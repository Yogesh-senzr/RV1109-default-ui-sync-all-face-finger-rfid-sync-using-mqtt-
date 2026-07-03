#include <QDialog>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QSettings>
#include <QDebug>
#include <QPixmap>
#include <QHBoxLayout>
#include "Config/ReadConfig.h"

class SyncFunctionality
{
public:
    static void ShowSyncDialog();
};

void SyncFunctionality::ShowSyncDialog()
{
    QDialog dialog;
    dialog.setWindowTitle("Device Synchronization");
    dialog.setFixedSize(400, 250);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    
    // Title Label - Modified
    QLabel *titleLabel = new QLabel("Sync Data");
    titleLabel->setStyleSheet("font-size: 22px; font-weight: bold; color: #333;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // Add some spacing
    layout->addSpacing(20);

    // Sync Toggle Button - Modified to use PNG images
    QPushButton *syncButton = new QPushButton;
    syncButton->setCheckable(true);
    syncButton->setFixedSize(60, 30);
    syncButton->setFlat(true); // Remove button border/background

    // Read current sync state - force fresh read from config
    bool isSyncEnabled = ReadConfig::GetInstance()->getSyncEnabled() != 0;
    qDebug() << "SYNC_DIALOG: Initial sync state from singleton:" << isSyncEnabled;
    
    syncButton->setChecked(isSyncEnabled);
    
    // Function to update button with PNG images
    auto updateButtonImage = [](QPushButton* button, bool enabled) {
        QPixmap pixmap;
        if (enabled) {
            // Load ON state PNG image
            pixmap.load(":/Images/wifiOn.png");
        } else {
            // Load OFF state PNG image
            pixmap.load(":/Images/wifiOff.png");
        }
        
        if (!pixmap.isNull()) {
            // Scale the image to fit the button
            QPixmap scaledPixmap = pixmap.scaled(button->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            button->setIcon(QIcon(scaledPixmap));
            button->setIconSize(button->size());
        } else {
            qDebug() << "Warning: Could not load toggle image";
            // Fallback to text if images not found
            button->setText(enabled ? "ON" : "OFF");
        }
    };
    
    // Apply initial image
    updateButtonImage(syncButton, isSyncEnabled);

    QLabel *syncStatusLabel = new QLabel(isSyncEnabled ? "Enabled" : "Disabled");
    syncStatusLabel->setStyleSheet("font-size: 16px; color: #555;");

    QObject::connect(syncButton, &QPushButton::clicked, [syncButton, syncStatusLabel, updateButtonImage]() {
        bool newState = syncButton->isChecked();
        qDebug() << "SYNC_DIALOG: Button clicked, new state:" << newState;
        
        syncStatusLabel->setText(newState ? "Enabled" : "Disabled");
        updateButtonImage(syncButton, newState);
        
        // Save the state immediately using singleton
        qDebug() << "SYNC_DIALOG: Saving state to singleton...";
        ReadConfig::GetInstance()->setSyncEnabled(newState ? 1 : 0);
        
        // Force save to file immediately
        ReadConfig::GetInstance()->setSaveConfig();
        
        // Verify the state was saved by reading it back
        bool savedState = ReadConfig::GetInstance()->getSyncEnabled() != 0;
        qDebug() << "SYNC_DIALOG: State verification - requested:" << newState << "saved:" << savedState;
        
        if (savedState != newState) {
            qDebug() << "ERROR: SYNC_DIALOG: State was not saved correctly!";
        } else {
            qDebug() << "SUCCESS: SYNC_DIALOG: State saved correctly";
        }
    });

    // Layout for switch and label - Centered
    QHBoxLayout *toggleLayout = new QHBoxLayout;
    toggleLayout->addStretch(); // Add stretch before
    toggleLayout->addWidget(syncButton);
    toggleLayout->addSpacing(10); // Small spacing between button and label
    toggleLayout->addWidget(syncStatusLabel);
    toggleLayout->addStretch(); // Add stretch after
    layout->addLayout(toggleLayout);

    // Add spacing before back button
    layout->addSpacing(30);

    // Back Button
    QPushButton *backButton = new QPushButton("Back");
    backButton->setStyleSheet(
        "padding: 10px; font-size: 16px; background-color:rgb(103, 135, 224); color: white; border-radius: 5px;"
    );

    QObject::connect(backButton, &QPushButton::clicked, [&]() {
        // Verify state before closing
        bool finalState = ReadConfig::GetInstance()->getSyncEnabled() != 0;
        qDebug() << "SYNC_DIALOG: Dialog closing - final sync state:" << finalState;
        dialog.close();
    });

    layout->addWidget(backButton);
    dialog.setLayout(layout);
    
    // Debug initial state
    qDebug() << "SYNC_DIALOG: Dialog opened - initial sync state:" << isSyncEnabled;
    
    dialog.exec();
    
    // Final debug after dialog closes
    bool finalState = ReadConfig::GetInstance()->getSyncEnabled() != 0;
    qDebug() << "SYNC_DIALOG: Dialog execution finished - final state:" << finalState;
}