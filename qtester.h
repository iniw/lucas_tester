#pragma once

#include <QMainWindow>
#include <QByteArray>

class QSerialPort;
class QTimer;
class QFile;
class QDataStream;
class QKeyEvent;
class QBluetoothSocket;
class QBluetoothDeviceDiscoveryAgent;
class QBluetoothLocalDevice;

QT_BEGIN_NAMESPACE
namespace Ui {
class QTester;
}
QT_END_NAMESPACE

class QTester : public QMainWindow {
    Q_OBJECT

public:
    QTester(QWidget* parent = nullptr);
    ~QTester();

private:
    void interpretLineFromMachine(QByteArray);

    void sendRawBufferToMachine(QByteArray);

    void streamAndConsumeBuffer(QByteArray&);

    void sendUserInput();

    void onSocketReady();

    void searchForDevice();

    void continueStreamingNewFirmware();

    bool eventFilter(QObject*, QEvent*) override;

private slots:
    void startFirmwareUpdate();

private:
    QByteArray mUserInputBuffer = {};

    std::unique_ptr<Ui::QTester> mUI;

    QBluetoothSocket* mSocket;
    QBluetoothDeviceDiscoveryAgent* mDiscoveryAgent;
    QBluetoothLocalDevice* mLocalDevice;

    QFile* mFirmwareFile;
    QDataStream* mFirmwareStream;
};
