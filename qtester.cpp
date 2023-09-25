#include "qtester.h"
#include "ui_qtester.h"
#include <QFile>
#include <QDataStream>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <QScrollBar>
#include <QKeyEvent>
#include <algorithm>
#include <filesystem>

constexpr auto FIRMWARE_PATH = "Robin_nano_V3.bin";
constexpr auto MAX_BUFFER_SIZE = qsizetype(64);

QTester::QTester(QWidget* parent)
    : QMainWindow(parent)
    , mTimer(std::make_unique<QTimer>(this))
    , mUI(std::make_unique<Ui::QTester>())
    , mFirmwareFile(nullptr)
    , mFirmwareStream(nullptr) {
    mUI->setupUi(this);
    mUI->statusBar->showMessage("searching...");

    mUI->userInput->installEventFilter(this);

    connect(
        mTimer.get(),
        &QTimer::timeout,
        this,
        &QTester::tryFetchingNewConnection);

    mTimer->start(1000);

    connect(
        mUI->clearLogSerial,
        &QPushButton::clicked,
        mUI->logSerial,
        &QTextEdit::clear);
}

static bool isValidDelimiter(char c) {
    constexpr auto DELIMITERS = std::array<char, 2>{ '#', '$' };
    return std::find(DELIMITERS.begin(), DELIMITERS.end(), c) != DELIMITERS.end();
}

void QTester::interpretLineFromMachine(QByteArray bytes) {
    qDebug() << "received: " << bytes;
    if (bytes == "ok") { // ne segredo
        if (mFirmwareFile) {
            continueStreamingNewFirmware();
        } else {
            streamAndConsumeBuffer(mUserInputBuffer);
        }
    } else {
        auto scrollbar = mUI->logSerial->verticalScrollBar();
        bool shouldScroll = scrollbar and scrollbar->value() == scrollbar->maximum();

        if (not isValidDelimiter(bytes.front())) {
            if (bytes.startsWith("ERRO:")) {
                const auto html = QString(R"(<p style="color:crimson;">%1<span style="color:white;">%2<br></p>)").arg(bytes.first(5), bytes.sliced(5));
                mUI->logSerial->insertHtml(html);
            } else if (isalpha(bytes[0]) and bytes[1] == ':') {
                const auto html = QString(R"(<p style="color:skyblue;">%1<span style="color:white;">%2<br></p>)").arg(bytes.first(2), bytes.sliced(2));
                mUI->logSerial->insertHtml(html);
            } else {
                const auto html = QString(R"(<p style="color:white;">%1<br></p>)").arg(bytes);
                mUI->logSerial->insertHtml(html);
            }
        } else {
            const auto html = QString(R"(<p style="color:gray;">%1<br></p>)").arg(bytes);
            mUI->logSerial->insertHtml(html);
        }

        if (shouldScroll)
            scrollbar->setValue(scrollbar->maximum());
    }
}

void QTester::sendUserInput() {
    if (not mPort or not mPort->isOpen())
        return;

    auto input = mUI->userInput->toPlainText().toLatin1();
    if (input.isEmpty())
        return;

    if (not isValidDelimiter(input.front())) {
        qDebug() << "cade o delimitador";
        mUI->userInput->clear();
        return;
    }

    if (input.back() != input.front())
        input.push_back(input.front());

    if (mUserInputBuffer.isEmpty())
        streamAndConsumeBuffer(input);

    mUserInputBuffer.append(input);
}

void QTester::streamAndConsumeBuffer(QByteArray& buffer) {
    if (buffer.isEmpty())
        return;

    const auto sliceSize = std::min(MAX_BUFFER_SIZE, buffer.size());
    auto slicedBuffer = buffer.first(sliceSize);
    if (slicedBuffer.back() == '$')
        slicedBuffer.insert(slicedBuffer.size() - 1, '\0');

    sendRawBufferToMachine(slicedBuffer);
    buffer.remove(0, sliceSize);
}

void QTester::sendRawBufferToMachine(QByteArray bytes) {
    if (not mPort or not mPort->isOpen())
        return;

    if (bytes.isEmpty())
        return;

    const auto sentBytes = mPort->write(bytes);
    if (sentBytes == -1) {
        qDebug() << "falha ao enviar - [" << mPort->errorString() << "]";
    } else {
        qDebug() << "enviados " << bytes.size() << "bytes";
    }
}

void QTester::startFirmwareUpdate() {
    if (not mPort or not mPort->isOpen())
        return;

    if (not std::filesystem::exists(FIRMWARE_PATH)) {
        qDebug() << "novo firmware nao foi encontrado";
        return;
    }

    const auto size = std::filesystem::file_size(FIRMWARE_PATH);
    if (size == 0) {
        qDebug() << "novo firmware esta vazio";
        return;
    }

    mFirmwareFile = std::make_unique<QFile>(FIRMWARE_PATH);
    mFirmwareFile->open(QFile::ReadOnly);
    mFirmwareStream = std::make_unique<QDataStream>(mFirmwareFile.get());

    auto command = QString("#{\"cmdFirmwareUpdate\":%1}#").arg(QString::number(size)).toLatin1();
    sendRawBufferToMachine(command);
}

void QTester::continueStreamingNewFirmware() {
    QByteArray buffer = {};
    buffer.reserve(MAX_BUFFER_SIZE);

    const auto bytesRead = mFirmwareStream->readRawData(buffer.data(), MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
        mFirmwareStream = nullptr;
        mFirmwareFile = nullptr;
        qDebug() << "erro no envio";
        return;
    }

    buffer.resize(bytesRead);
    sendRawBufferToMachine(buffer);

    if (mFirmwareStream->atEnd()) {
        mFirmwareStream = nullptr;
        mFirmwareFile = nullptr;
        qDebug() << "fim do envio";
    }
}

void QTester::onPortReady() {
    if (not mPort || not mPort->isOpen())
        return;

    while (mPort->canReadLine())
        interpretLineFromMachine(mPort->readLine().simplified());
}

void QTester::tryFetchingNewConnection() {
    if (mPort) {
        if (not hasValidPort()) {
            mUI->statusBar->showMessage("lost connection");
            mPort = nullptr;
        }
    } else {
        mUI->statusBar->showMessage("searching...");
        mPort = tryFindValidPort();
        if (not mPort) {
            return;
        }
        mUI->logSerial->hasFocus();
        mPort->setBaudRate(115200);
        if (not mPort->open(QSerialPort::ReadWrite)) {
            mUI->statusBar->showMessage("failed to open valid port");
        } else {
            mUI->logSerial->insertHtml(R"(<p style="color:pink;">~~NEW~CONNECTION~~<br></p>)");
            mUI->statusBar->showMessage(QString("%1 | %2").arg(
                mPort->portName(),
                QString::number(mPort->baudRate())));
            QObject::connect(mPort.get(), &QSerialPort::readyRead, this, &QTester::onPortReady);
        }
    }
}

bool QTester::eventFilter(QObject* object, QEvent* event) {
    if (event->type() != QEvent::KeyPress)
        return false;

    auto keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->modifiers() or (keyEvent->key() != Qt::Key_Return and keyEvent->key() != Qt::Key_Enter))
        return QObject::eventFilter(object, event);

    sendUserInput();
    return true;
}

template<typename FN>
static void findValidPort(FN callback) {
    auto list = QSerialPortInfo::availablePorts();
    if (list.empty())
        return;

    for (auto& portInfo : list) {
        if (portInfo.vendorIdentifier() == 1155) {
            callback(portInfo);
            return;
        }
    }
}

std::unique_ptr<QSerialPort> QTester::tryFindValidPort() {
    std::unique_ptr<QSerialPort> port = nullptr;
    findValidPort([&port, this](auto& portInfo) {
        port = std::make_unique<QSerialPort>(portInfo, this);
    });
    return port;
}

bool QTester::hasValidPort() {
    bool found = false;
    findValidPort([&found](auto& portInfo) {
        found = true;
    });
    return found;
}

QTester::~QTester() = default;
