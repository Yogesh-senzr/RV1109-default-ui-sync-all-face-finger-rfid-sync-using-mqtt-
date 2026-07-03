#ifndef IDENTIFYSETUPFRM_H
#define IDENTIFYSETUPFRM_H

#include "SettingFuncFrms/SettingBaseFrm.h"
#include <QListWidgetItem>

class ConnHttpServerThread;

//识别设置UI
class QListWidgetItem;
class IdentifySetupFrmPrivate;
class IdentifySetupFrm : public SettingBaseFrm
{
    Q_OBJECT
public:
    explicit IdentifySetupFrm(QWidget *parent = nullptr);
    ~IdentifySetupFrm();
private:
    virtual void setEnter();//进入
    virtual void setLeaveEvent();//退出
private:
    Q_SLOT void slotIemClicked(QListWidgetItem *item);
private:
    QScopedPointer<IdentifySetupFrmPrivate>d_ptr;
#ifdef SCREENCAPTURE  //ScreenCapture       
    void mouseDoubleClickEvent(QMouseEvent*);          
#endif    
private:
    Q_DECLARE_PRIVATE(IdentifySetupFrm)
    Q_DISABLE_COPY(IdentifySetupFrm)
    ConnHttpServerThread* getHttpServerThread();
};

#endif // IDENTIFYSETUPFRM_H