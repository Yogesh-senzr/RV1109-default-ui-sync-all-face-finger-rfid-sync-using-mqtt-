#include "PostPersonRecordThread.h"
#include "Version.h"
#include "Application/FaceApp.h"
#include "Config/ReadConfig.h"
#include "PCIcore/Watchdog.h"
#include "MessageHandler/Log.h"
#include "SharedInclude/GlobalDef.h"
#include "Helper/myhelper.h"
#include "PCIcore/RkUtils.h"
#include "PCIcore/Utils_Door.h"
#include "DB/RegisteredFacesDB.h"
#include "ManageEngines/PersonRecordToDB.h"
#include "RKCamera/Camera/cameramanager.h"
#include "base64.hpp"
#include "json-cpp/json.h"
#include <mosquitto.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <openssl/md5.h>
#include <QtCore/QTextStream>
#include <QtCore/QDir>
#include <QtCore/QFileInfoList>
#include <QtCore/QDebug>

class PostPersonRecordThreadPrivate
{
	Q_DECLARE_PUBLIC(PostPersonRecordThread)
public:
	PostPersonRecordThreadPrivate(PostPersonRecordThread *dd);
	~PostPersonRecordThreadPrivate();
	
	int doPostPersonRecord();
	bool initializeMqtt();
	bool publishAttendanceLog(const Json::Value& json);

	QString mSN;
	QString mPostPersonRecordServerUrl;
	QString mPostPersonRecordServerPassword;

	// MQTT members
	struct mosquitto* m_mosq;
	QString m_mqttBroker;
	int m_mqttPort;
	QString m_mqttUsername;
	QString m_mqttPassword;
	QString m_clientId;
	bool m_mqttConnected;

private:
	mutable QMutex sync;
	QWaitCondition pauseCond;
	int threadDelay;
	QList<QString> picList;

	// MQTT callbacks
	static void onMqttConnect(struct mosquitto* mosq, void* obj, int reasonCode);
	static void onMqttDisconnect(struct mosquitto* mosq, void* obj, int reasonCode);
	static void onMqttPublish(struct mosquitto* mosq, void* obj, int mid);
	static void onMqttMessage(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message);
	static void onMqttLog(struct mosquitto* mosq, void* obj, int level, const char* str);

private:
	PostPersonRecordThread * const q_ptr;
};

typedef struct _MESSAGE_HEADER_S
{
	std::string msg_type;
	std::string sn;
	std::string timestamp;
	std::string password;
	std::string cmd_id;
} MESSAGE_HEADER_S;

static std::string getTime()
{
	time_t timep;
	time(&timep);
	char tmp[64];
	strftime(tmp, sizeof(tmp), "%Y%m%d%H%M%S", localtime(&timep));
	return tmp;
}

static std::string md5sum(std::string src)
{
	std::string dst = "";
	unsigned char outmd[16] = { 0 };
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx, src.c_str(), src.length());
	MD5_Final(outmd, &ctx);
	for (int i = 0; i < 16; i++)
	{
		char tmp[4] = { 0 };
		sprintf(tmp, "%02x", (unsigned char) outmd[i]);
		dst += tmp;
	}
	return dst;
}

static std::string getTime3()
{
	time_t timep;
	time(&timep);
	char tmp[64];
	strftime(tmp, sizeof(tmp), "%Y/%m/%d %H:%M:%S", localtime(&timep));
	return tmp;
}

static std::string fileToBase64String(std::string strFilePath)
{
	std::string base_64;

	if (strFilePath.length() > 3)
	{
		std::string cmd = "/bin/chmod 777 " + strFilePath;
		YNH_LJX::RkUtils::Utils_ExecCmd(cmd.c_str());
	}

	struct stat _stat;
	stat(strFilePath.c_str(), &_stat);

	if (_stat.st_size > 0)
	{
		FILE *pFile = fopen(strFilePath.c_str(), "rb");
		if (pFile != ISC_NULL)
		{
			char *pTmpBuf = (char*) malloc(_stat.st_size);
			if (pTmpBuf != ISC_NULL)
			{
				long nTotalNum = 0;
				while (true)
				{
					int ret = fread(pTmpBuf + nTotalNum, 1, _stat.st_size, pFile);
					nTotalNum += ret;
					if (nTotalNum >= _stat.st_size)
					{
						break;
					}
				}
				base_64 = cereal::base64::encode((unsigned char const*) pTmpBuf, (size_t) _stat.st_size);
				free(pTmpBuf);
			}
			fclose(pFile);
		}
	}
	return base_64;
}

PostPersonRecordThreadPrivate::PostPersonRecordThreadPrivate(PostPersonRecordThread *dd) :
		q_ptr(dd),
		threadDelay(1),
		m_mosq(nullptr),
		m_mqttPort(1883),
		m_mqttConnected(false)
{
	mPostPersonRecordServerUrl = "";
	mPostPersonRecordServerPassword = "";

	// MQTT Configuration
	m_mqttBroker = "mqtt.fieldseasy.com";
	m_mqttPort = 1883;
	m_mqttUsername = "iot-device";
	m_mqttPassword = "Senzr123";

	qRegisterMetaType<IdentifyFaceRecord_t>("IdentifyFaceRecord_t");
	QObject::connect(q_func(), &PostPersonRecordThread::sigAppRecordData, q_func(), &PostPersonRecordThread::slotAppRecordData);

	// Initialize MQTT
	mosquitto_lib_init();
	LogD("%s %s[%d] MQTT library initialized\n", __FILE__, __FUNCTION__, __LINE__);
}

PostPersonRecordThreadPrivate::~PostPersonRecordThreadPrivate()
{
	if (m_mosq)
	{
		mosquitto_loop_stop(m_mosq, true);
		mosquitto_disconnect(m_mosq);
		mosquitto_destroy(m_mosq);
		m_mosq = nullptr;
	}
	mosquitto_lib_cleanup();
	LogD("%s %s[%d] MQTT cleanup completed\n", __FILE__, __FUNCTION__, __LINE__);
}

bool PostPersonRecordThreadPrivate::initializeMqtt()
{
	LogD("%s %s[%d] Initializing MQTT connection\n", __FILE__, __FUNCTION__, __LINE__);

	// Generate client ID using device serial number
	m_clientId = "rv1109_" + mSN;
	LogD("%s %s[%d] Client ID: %s\n", __FILE__, __FUNCTION__, __LINE__, m_clientId.toStdString().c_str());

	// If we already have a client, destroy it
	if (m_mosq)
	{
		mosquitto_loop_stop(m_mosq, true);
		mosquitto_disconnect(m_mosq);
		mosquitto_destroy(m_mosq);
		m_mosq = nullptr;
		m_mqttConnected = false;
	}

	// Create new client instance
	m_mosq = mosquitto_new(m_clientId.toStdString().c_str(), true, this);
	if (!m_mosq)
	{
		LogE("%s %s[%d] Failed to create mosquitto instance\n", __FILE__, __FUNCTION__, __LINE__);
		return false;
	}

	// Set username and password
	if (mosquitto_username_pw_set(m_mosq, m_mqttUsername.toStdString().c_str(), 
	                               m_mqttPassword.toStdString().c_str()) != MOSQ_ERR_SUCCESS)
	{
		LogE("%s %s[%d] Failed to set MQTT credentials\n", __FILE__, __FUNCTION__, __LINE__);
		return false;
	}

	// Set callbacks
	mosquitto_connect_callback_set(m_mosq, onMqttConnect);
	mosquitto_disconnect_callback_set(m_mosq, onMqttDisconnect);
	mosquitto_publish_callback_set(m_mosq, onMqttPublish);
	mosquitto_message_callback_set(m_mosq, onMqttMessage);
	mosquitto_log_callback_set(m_mosq, onMqttLog);

	// Connect to broker
	LogD("%s %s[%d] Connecting to broker %s:%d\n", __FILE__, __FUNCTION__, __LINE__, 
	     m_mqttBroker.toStdString().c_str(), m_mqttPort);
	
	int rc = mosquitto_connect(m_mosq, m_mqttBroker.toStdString().c_str(), m_mqttPort, 60);
	if (rc != MOSQ_ERR_SUCCESS)
	{
		LogE("%s %s[%d] Failed to connect: %s\n", __FILE__, __FUNCTION__, __LINE__, 
		     mosquitto_strerror(rc));
		return false;
	}

	// Start network loop
	rc = mosquitto_loop_start(m_mosq);
	if (rc != MOSQ_ERR_SUCCESS)
	{
		LogE("%s %s[%d] Failed to start loop: %s\n", __FILE__, __FUNCTION__, __LINE__, 
		     mosquitto_strerror(rc));
		return false;
	}

	LogD("%s %s[%d] MQTT initialization completed\n", __FILE__, __FUNCTION__, __LINE__);
	return true;
}

bool PostPersonRecordThreadPrivate::publishAttendanceLog(const Json::Value& json)
{
	if (!m_mosq || !m_mqttConnected)
	{
		LogE("%s %s[%d] MQTT not connected, cannot publish\n", __FILE__, __FUNCTION__, __LINE__);
		return false;
	}

	// Convert JSON to string
	Json::FastWriter fast_writer;
	std::string jsonStr = fast_writer.write(json);
	
	// Remove trailing newline
	if (!jsonStr.empty() && jsonStr[jsonStr.length() - 1] == '\n')
	{
		jsonStr = jsonStr.substr(0, jsonStr.length() - 1);
	}

	// Create topic: devices/rv1109/{device_sn}/attendance_log
	std::string topic = "devices/rv1109/" + mSN.toStdString() + "/attendance_log";
	
	LogD("%s %s[%d] Publishing to topic: %s\n", __FILE__, __FUNCTION__, __LINE__, topic.c_str());
	LogD("%s %s[%d] Payload: %s\n", __FILE__, __FUNCTION__, __LINE__, jsonStr.c_str());

	// Publish with QoS 1
	int rc = mosquitto_publish(m_mosq, nullptr, topic.c_str(), jsonStr.length(), 
	                           jsonStr.c_str(), 1, false);
	
	if (rc != MOSQ_ERR_SUCCESS)
	{
		LogE("%s %s[%d] Failed to publish: %s\n", __FILE__, __FUNCTION__, __LINE__, 
		     mosquitto_strerror(rc));
		return false;
	}

	LogD("%s %s[%d] Attendance log published successfully\n", __FILE__, __FUNCTION__, __LINE__);
	return true;
}

void PostPersonRecordThreadPrivate::onMqttConnect(struct mosquitto* mosq, void* obj, int reasonCode)
{
	PostPersonRecordThreadPrivate* instance = static_cast<PostPersonRecordThreadPrivate*>(obj);
	
	LogD("%s %s[%d] MQTT connect callback, reason code: %d\n", __FILE__, __FUNCTION__, __LINE__, reasonCode);
	
	if (reasonCode == 0)
	{
		LogD("%s %s[%d] Successfully connected to MQTT broker\n", __FILE__, __FUNCTION__, __LINE__);
		instance->m_mqttConnected = true;
		
		// Subscribe to attendance response topic: devices/rv1109/{device_sn}/attendance_response
		std::string responseTopic = "devices/rv1109/" + instance->mSN.toStdString() + "/attendance_response";
		
		int rc = mosquitto_subscribe(mosq, nullptr, responseTopic.c_str(), 1);
		if (rc == MOSQ_ERR_SUCCESS)
		{
			LogD("%s %s[%d] Subscribed to topic: %s\n", __FILE__, __FUNCTION__, __LINE__, 
			     responseTopic.c_str());
		}
		else
		{
			LogE("%s %s[%d] Failed to subscribe: %s\n", __FILE__, __FUNCTION__, __LINE__, 
			     mosquitto_strerror(rc));
		}
	}
	else
	{
		LogE("%s %s[%d] Connection failed with reason code: %d\n", __FILE__, __FUNCTION__, __LINE__, 
		     reasonCode);
		instance->m_mqttConnected = false;
	}
}

void PostPersonRecordThreadPrivate::onMqttDisconnect(struct mosquitto* mosq, void* obj, int reasonCode)
{
	PostPersonRecordThreadPrivate* instance = static_cast<PostPersonRecordThreadPrivate*>(obj);
	
	LogD("%s %s[%d] Disconnected from MQTT broker, reason code: %d\n", __FILE__, __FUNCTION__, __LINE__, 
	     reasonCode);
	instance->m_mqttConnected = false;
	
	// Try to reconnect if disconnection was unexpected
	if (reasonCode != 0)
	{
		LogD("%s %s[%d] Attempting to reconnect\n", __FILE__, __FUNCTION__, __LINE__);
		int rc = mosquitto_reconnect(mosq);
		if (rc != MOSQ_ERR_SUCCESS)
		{
			LogE("%s %s[%d] Reconnection failed: %s\n", __FILE__, __FUNCTION__, __LINE__, 
			     mosquitto_strerror(rc));
		}
	}
}

void PostPersonRecordThreadPrivate::onMqttPublish(struct mosquitto* mosq, void* obj, int mid)
{
	LogD("%s %s[%d] Message published successfully, id: %d\n", __FILE__, __FUNCTION__, __LINE__, mid);
}

void PostPersonRecordThreadPrivate::onMqttMessage(struct mosquitto* mosq, void* obj, 
                                                   const struct mosquitto_message* message)
{
	PostPersonRecordThreadPrivate* instance = static_cast<PostPersonRecordThreadPrivate*>(obj);
	
	LogD("%s %s[%d] Received message on topic: %s\n", __FILE__, __FUNCTION__, __LINE__, 
	     message->topic);
	
	if (message->payloadlen > 0)
	{
		std::string payload(static_cast<const char*>(message->payload), message->payloadlen);
		LogD("%s %s[%d] Payload: %s\n", __FILE__, __FUNCTION__, __LINE__, payload.c_str());
		
		// Parse JSON response
		Json::Reader reader;
		Json::Value json;
		if (reader.parse(payload, json))
		{
			if (json.isMember("msg_type") && 
			    json["msg_type"].asString() == "attendance_response_done")
			{
				LogD("%s %s[%d] Attendance response received\n", __FILE__, __FUNCTION__, __LINE__);
				
				// Process response and update database
				if (json.isMember("rID"))
				{
					QString rIDs = QString::fromStdString(json["rID"].asString());
					QStringList sections = rIDs.split(",");
					
					for (int i = 0; i < sections.size(); i++)
					{
						int rid = sections[i].toInt();
						LogD("%s %s[%d] Updating rid %d as uploaded\n", __FILE__, __FUNCTION__, __LINE__, rid);
						PersonRecordToDB::GetInstance()->UpdatePersonRecordUploadFlag(rid, true);
					}
				}
			}
		}
	}
}

void PostPersonRecordThreadPrivate::onMqttLog(struct mosquitto* mosq, void* obj, int level, const char* str)
{
	switch (level)
	{
		case MOSQ_LOG_INFO:
		case MOSQ_LOG_NOTICE:
			LogD("MQTT_LOG: %s\n", str);
			break;
		case MOSQ_LOG_WARNING:
			LogD("MQTT_WARNING: %s\n", str);
			break;
		case MOSQ_LOG_ERR:
			LogE("MQTT_ERROR: %s\n", str);
			break;
		case MOSQ_LOG_DEBUG:
			// Only log debug in verbose mode
			break;
	}
}

int PostPersonRecordThreadPrivate::doPostPersonRecord()
{
	int delay = 5;
	
	// Initialize MQTT if not already done
	if (!m_mosq)
	{
		if (!initializeMqtt())
		{
			LogE("%s %s[%d] Failed to initialize MQTT\n", __FILE__, __FUNCTION__, __LINE__);
			return 60; // Retry after 60 seconds
		}
	}

	// Check if MQTT is connected
	if (!m_mqttConnected)
	{
		LogD("%s %s[%d] MQTT not connected, waiting...\n", __FILE__, __FUNCTION__, __LINE__);
		return 10; // Retry after 10 seconds
	}

	std::string start_time = "1979/01/01 00:00:01";
	std::string end_time = getTime3();

	int nPerPage = 1;
	int nTotalCount = 0;
	int nDataCount = 0;
	QList<IdentifyFaceRecord_t> list;
	QDateTime startDateTime = QDateTime::fromString(start_time.c_str(), "yyyy/MM/dd hh:mm:ss");
	QDateTime endDateTime = QDateTime::fromString(end_time.c_str(), "yyyy/MM/dd hh:mm:ss");

	nTotalCount = PersonRecordToDB::GetInstance()->GetPersonRecordTotalNumByDateTime(startDateTime, endDateTime, true);
	list = PersonRecordToDB::GetInstance()->GetPersonRecordDataByDateTime(1, nPerPage, startDateTime, endDateTime, true);
	nDataCount = list.size();

	Json::Value json;
	std::string timestamp;
	std::string password;
	
	timestamp = getTime();
	password = mPostPersonRecordServerPassword.toStdString() + timestamp;
	password = md5sum(password);
	password = md5sum(password);
	transform(password.begin(), password.end(), password.begin(), ::tolower);
	
	json["msg_type"] = "face_post_offine_record";
	json["sn"] = mSN.toStdString().c_str();
	json["timestamp"] = timestamp.c_str();
	json["password"] = password.c_str();
	json["cmd_id"] = "";
	json["dev_name"] = "";
	json["location"] = "";
	json["message"] = "post_offine_record";
	json["per_page"] = "0";
	json["total_page"] = "0";
	json["result"] = "0";
	json["success"] = "0";
	
	long rid = 0;
	if (nTotalCount > 0 && nDataCount > 0)
	{
		for (int i = 0; i < nDataCount; i++)
		{
			if (i >= list.size())
			{
				break;
			}
			auto &t = list[i];

			// Check if already posted
			int nArleadyPostPersonRecordIndex = -1;
			for (int i = 0; i < picList.size(); i++)
			{
				if (picList[i] == t.FaceImgPath)
				{
					nArleadyPostPersonRecordIndex = i;
					LogD("%s %s[%d] already posted rid %s %s\n", __FILE__, __FUNCTION__, __LINE__, 
					     std::to_string(t.rid).c_str(), t.FaceImgPath.toStdString().c_str());
					break;
				}
			}
			
			if (nArleadyPostPersonRecordIndex > -1)
			{
				picList.removeAt(nArleadyPostPersonRecordIndex);
				PersonRecordToDB::GetInstance()->UpdatePersonRecordUploadFlag(t.rid, true);
				return delay;
			}

			Json::Value data;
			rid = t.rid;

			if (t.FaceImgPath != "/mnt/user/face_crop_image")
			{
				data["crop_data"] = fileToBase64String(t.FaceImgPath.toStdString().c_str());
			}

			data["card_no"] = t.face_iccardnum.toStdString().c_str();
			data["male"] = t.face_sex.toStdString().c_str();
			data["id_card_no"] = t.face_idcardnum.toStdString().c_str();
			data["person_name"] = t.face_name.toStdString().c_str();
			data["time"] = t.createtime.toString("yyyy/MM/dd hh:mm:ss").toStdString().c_str();
			data["person_uuid"] = t.face_uuid.toStdString().c_str();
			data["Identifyed"] = std::to_string(t.Identifyed).c_str();
			data["rID"] = std::to_string(rid).c_str();
			data["group"] = t.face_gids.toStdString().c_str();
			data["person_type"] = std::to_string(t.face_persontype).c_str();
			data["face_mack"] = std::to_string(t.face_mack).c_str();
			data["temp_value"] = std::to_string(t.temp_value).c_str();
			json["data"].append(data);
		}
		
		json["count"] = std::to_string(nDataCount).c_str();
		json["total_page"] = std::to_string(nTotalCount / nPerPage);
		json["result"] = "1";
		json["success"] = "1";
		
		// Publish to MQTT instead of HTTP
		if (publishAttendanceLog(json))
		{
			LogD("%s %s[%d] Attendance log published successfully\n", __FILE__, __FUNCTION__, __LINE__);
			delay = 5;
		}
		else
		{
			LogE("%s %s[%d] Failed to publish attendance log\n", __FILE__, __FUNCTION__, __LINE__);
			delay = 10;
		}
	}
	else
	{
		delay = 60;
	}
	
	return delay;
}

PostPersonRecordThread::PostPersonRecordThread(QObject *parent) :
		QThread(parent),
		d_ptr(new PostPersonRecordThreadPrivate(this))
{
	this->start();
}

PostPersonRecordThread::~PostPersonRecordThread()
{
	Q_D(PostPersonRecordThread);
	this->requestInterruption();
	d->pauseCond.wakeOne();

	this->quit();
	this->wait();
}

void PostPersonRecordThread::run()
{
	Q_D(PostPersonRecordThread);
	sleep(20);
	
	while (true)
	{
		d->sync.lock();
		int delay = 10;
		d->mSN = myHelper::getCpuSerial();
		d->mPostPersonRecordServerPassword = ReadConfig::GetInstance()->getPost_PersonRecord_Password();

		delay = d->doPostPersonRecord();
		
		d->pauseCond.wait(&d->sync, delay * 1000);
		d->sync.unlock();
	}
}

void PostPersonRecordThread::slotAppRecordData(const IdentifyFaceRecord_t t)
{
	Q_D(PostPersonRecordThread);

	Json::Value root;
	std::string timestamp;
	std::string password;

	timestamp = getTime();
	std::string currentTimeFormatted = getTime3();

	d->mPostPersonRecordServerPassword = ReadConfig::GetInstance()->getPost_PersonRecord_Password();
	
	// Initialize MQTT if needed
	if (!d->m_mosq)
	{
		d->initializeMqtt();
	}

	if (!d->m_mqttConnected)
	{
		LogE("%s %s[%d] MQTT not connected, cannot send real-time attendance\n", 
		     __FILE__, __FUNCTION__, __LINE__);
		return;
	}

	QString mMustmode = ReadConfig::GetInstance()->getDoor_MustOpenMode();
	QString mOptionmode = ReadConfig::GetInstance()->getDoor_OptionalOpenMode();

	// Handle case with no face image
	if (t.FaceImgPath == "/mnt/user/face_crop_image")
	{
		timestamp = getTime();
		password = d->mPostPersonRecordServerPassword.toStdString() + timestamp;
		password = md5sum(password);
		password = md5sum(password);
		transform(password.begin(), password.end(), password.begin(), ::tolower);
		
		root["msg_type"] = "face_post_record";
		root["sn"] = d->mSN.toStdString().c_str();
		root["timestamp"] = timestamp.c_str();
		root["time"] = currentTimeFormatted.c_str();
		root["password"] = password.c_str();
		root["type"] = std::to_string(t.passType).c_str();
		root["cmd_id"] = "";
		root["dev_name"] = "";
		root["location"] = "";
		root["message"] = "post_record";

		Json::Value data;
		data["person_uuid"] = t.face_uuid.toStdString().c_str();
		data["card_no"] = t.face_iccardnum.toStdString().c_str();
		data["id_card_no"] = t.face_idcardnum.toStdString().c_str();
		data["person_name"] = t.face_name.toStdString().c_str();
		data["person_type"] = std::to_string(t.face_persontype).c_str();
		data["male"] = t.face_sex.toStdString().c_str();
		data["face_mack"] = std::to_string(t.face_mack).c_str();
		data["temp_value"] = std::to_string(t.temp_value).c_str();
		data["time"] = currentTimeFormatted.c_str();
		data["Identifyed"] = std::to_string(t.Identifyed).c_str();
		data["rID"] = std::to_string(t.rid).c_str();

		root["data"].append(data);
		root["count"] = "1";
		root["result"] = "1";
		root["success"] = "1";
		
		d->publishAttendanceLog(root);
		return;
	}

	// Check if face image exists
	if (access(t.FaceImgPath.toStdString().c_str(), F_OK))
	{
		LogE("%s %s[%d] %s %s not exist, so ignore\n", __FILE__, __FUNCTION__, __LINE__,
		     t.face_name.toStdString().c_str(), t.FaceImgPath.toStdString().c_str());
		return;
	}

	timestamp = getTime();
	password = d->mPostPersonRecordServerPassword.toStdString() + timestamp;
	password = md5sum(password);
	password = md5sum(password);
	transform(password.begin(), password.end(), password.begin(), ::tolower);
	
	root["msg_type"] = "face_post_record";
	root["sn"] = d->mSN.toStdString().c_str();
	root["timestamp"] = timestamp.c_str();
	root["password"] = password.c_str();
	root["time"] = currentTimeFormatted.c_str();
	root["type"] = std::to_string(t.passType).c_str();
	root["cmd_id"] = "";
	root["dev_name"] = "";
	root["location"] = "";
	root["message"] = "post_record";

	Json::Value data;

	QString faceImgPath = "";
	if (ReadConfig::GetInstance()->getRecords_Manager_FaceImg() == 1)
	{
		faceImgPath = t.FaceImgPath;
	}
	else if (ReadConfig::GetInstance()->getRecords_Manager_PanoramaImg() == 1)
	{
		faceImgPath = t.FaceFullImgPath;
	}

	if (!access(faceImgPath.toStdString().c_str(), F_OK))
	{
		data["crop_data"] = fileToBase64String(faceImgPath.toStdString());
	}

	data["person_uuid"] = t.face_uuid.toStdString().c_str();
	data["card_no"] = t.face_iccardnum.toStdString().c_str();
	data["id_card_no"] = t.face_idcardnum.toStdString().c_str();
	data["person_name"] = t.face_name.toStdString().c_str();
	data["person_type"] = std::to_string(t.face_persontype).c_str();
	data["male"] = t.face_sex.toStdString().c_str();
	data["face_mack"] = std::to_string(t.face_mack).c_str();
	data["temp_value"] = std::to_string(t.temp_value).c_str();
	data["time"] = currentTimeFormatted.c_str();
	data["Identifyed"] = std::to_string(t.Identifyed).c_str();
	data["rID"] = std::to_string(t.rid).c_str();
	data["group"] = t.face_gids.toStdString().c_str();

	root["data"].append(data);
	root["count"] = "1";
	root["result"] = "1";
	root["success"] = "1";
	
	// Publish to MQTT
	if (d->publishAttendanceLog(root))
	{
		LogD("%s %s[%d] Real-time attendance published successfully, rid=%ld\n", 
		     __FILE__, __FUNCTION__, __LINE__, t.rid);
		d->picList.append(faceImgPath);
	}
	else
	{
		LogE("%s %s[%d] Failed to publish real-time attendance, rid=%ld\n", 
		     __FILE__, __FUNCTION__, __LINE__, t.rid);
	}
}
