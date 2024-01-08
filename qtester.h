#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <memory>
#ifdef ANDROID
    #include <QJniEnvironment>
#endif

class QSerialPort;
class QTimer;
class QFile;
class QDataStream;
class QKeyEvent;
class QBluetoothSocket;
class QBluetoothDeviceDiscoveryAgent;

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

    static inline QTester* s_instance = nullptr;

    static QTester& the() {
        return *s_instance;
    }

private:
    bool eventFilter(QObject*, QEvent* event) override;

    void interpretLineFromMachine(QByteArray);

    void continueStreamingNewFirmware();

    void sendRawBufferToMachine(QByteArray);

    void streamAndConsumeBuffer(QByteArray&);

    void sendUserInput();

    void onPortReady();

    void searchForDevice();

private slots:
    void startFirmwareUpdate();

private:
    QByteArray mUserInputBuffer = {};

    std::unique_ptr<Ui::QTester> mUI;

    std::unique_ptr<QBluetoothSocket> mSocket;
    std::unique_ptr<QBluetoothDeviceDiscoveryAgent> mDiscoveryAgent;

    std::unique_ptr<QFile> mFirmwareFile;
    std::unique_ptr<QDataStream> mFirmwareStream;
};
