#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <memory>

class QSerialPort;
class QTimer;
class QFile;
class QDataStream;
class QKeyEvent;

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
    bool eventFilter(QObject*, QEvent* event) override;

    void interpretLineFromMachine(QByteArray);

    void continueStreamingNewFirmware();

    void sendRawBufferToMachine(QByteArray);

    void streamAndConsumeBuffer(QByteArray&);

    void sendUserInput();

    std::unique_ptr<QSerialPort> tryFindValidPort();
    bool hasValidPort();

private slots:
    void startFirmwareUpdate();

    void tryFetchingNewConnection();

    void onPortReady();

private:
    QByteArray mUserInputBuffer = {};

    std::unique_ptr<QSerialPort> mPort;
    std::unique_ptr<QTimer> mTimer;
    std::unique_ptr<Ui::QTester> mUI;

    std::unique_ptr<QFile> mFirmwareFile;
    std::unique_ptr<QDataStream> mFirmwareStream;
};
