#include "qtester.h"
#include "ui_qtester.h"
#include <QFile>
#include <QDataStream>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QLatin1StringView>
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

    connect(
        mUI->testPump,
        &QPushButton::pressed,
        this,
        [this] { sendRawBufferToMachine(QString("$L5 T0 S1 V%1$").arg(mUI->pumpDigitalValue->value()).toLatin1()); });

    connect(
        mUI->testPump,
        &QPushButton::released,
        this,
        [this] { sendRawBufferToMachine("$L5 T0 S0$"); });

    connect(
        mUI->testResistance,
        &QPushButton::pressed,
        this,
        [this] { sendRawBufferToMachine("$L5 T1 S1$"); });

    connect(
        mUI->testResistance,
        &QPushButton::released,
        this,
        [this] { sendRawBufferToMachine("$L5 T1 S0$"); });

    connect(
        mUI->testBeeper,
        &QPushButton::pressed,
        this,
        [this] { sendRawBufferToMachine(QString("$L5 T6 S1 V%1$").arg(mUI->beeperFrequency->value()).toLatin1()); });

    connect(
        mUI->testBeeper,
        &QPushButton::released,
        this,
        [this] { sendRawBufferToMachine("$L5 T6 S0$"); });

    connect(
        mUI->logFlowSensor,
        &QCheckBox::clicked,
        this,
        [this] {
            auto gcode = QString("$L5 T4 S%1$").arg(int(mUI->logFlowSensor->isChecked()));
            sendRawBufferToMachine(gcode.toLatin1());
        });

    connect(
        mUI->logTemperature,
        &QCheckBox::clicked,
        this,
        [this] {
            auto gcode = QString("$L5 T5 S%1$").arg(int(mUI->logTemperature->isChecked()));
            sendRawBufferToMachine(gcode.toLatin1());
        });

    connect(
        mUI->xAxisLeft,
        &QToolButton::clicked,
        this,
        [this] { sendRawBufferToMachine("$G0 X-1$"); });

    connect(
        mUI->xAxisRight,
        &QToolButton::clicked,
        this,
        [this] { sendRawBufferToMachine("$G0 X1$"); });

    connect(
        mUI->yAxisDown,
        &QToolButton::clicked,
        this,
        [this] { sendRawBufferToMachine("$G0 Y-1$"); });

    connect(
        mUI->yAxisUp,
        &QToolButton::clicked,
        this,
        [this] { sendRawBufferToMachine("$G0 Y1$"); });

    const auto changeLedText = [this](int index) {
        auto str1 = QString("Testar LED do botão %1").arg(index + 1);
        auto str2 = QString("Testar PowerLED %1").arg(index + 1);
        mUI->testButtonLed->setText(str1);
        mUI->testPowerLed->setText(str2);
    };

    changeLedText(mUI->selectedStation->currentIndex());

    connect(
        mUI->selectedStation,
        &QComboBox::currentIndexChanged,
        this,
        changeLedText);

    connect(
        mUI->testButtonLed,
        &QPushButton::clicked,
        this,
        [this] {
            auto gcode = QString("$L5 T2 V%1$").arg(mUI->selectedStation->currentIndex());
            sendRawBufferToMachine(gcode.toLatin1());
        });

    connect(
        mUI->testPowerLed,
        &QPushButton::clicked,
        this,
        [this] {
            auto gcode = QString("$L5 T3 V%1$").arg(mUI->selectedStation->currentIndex());
            sendRawBufferToMachine(gcode.toLatin1());
        });

    const auto changePinCommandText = [this](int index) {
        bool is_output = index == 0x1;
        mUI->outputWidget->setEnabled(is_output);
        mUI->sendPinCommand->setText(is_output ? "Enviar valor digital" : "Ler valor digital");
    };

    changePinCommandText(mUI->pinCfg->currentIndex());
    connect(
        mUI->pinCfg,
        &QComboBox::currentIndexChanged,
        this,
        changePinCommandText);

    connect(
        mUI->sendPinCommand,
        &QPushButton::clicked,
        this,
        [this] {
            const bool is_output = mUI->pinCfg->currentIndex() == 0x1;
            auto text = mUI->pinName->text();
            if (text.isEmpty() || text.size() < 3 || text.front() != 'P' || (text[1] < 'A' || text[1] > 'E'))
                return;

            auto pin_number = [this] {
                const auto latin1 = mUI->pinName->text().toLatin1();

                bool ok;
                const auto pin_id = latin1.sliced(2).toInt(&ok);
                if (not ok || pin_id > 15)
                    return -1;

                const auto letter_number = latin1[1] - 'A';
                return letter_number * 15 + letter_number + pin_id;
            }();

            if (pin_number == -1)
                return;

            if (is_output) {
                auto gcode = QString("$L4 Z3 P%1 M%2 V%3$")
                                 .arg(pin_number)
                                 .arg(mUI->pinCfg->currentIndex())
                                 .arg(mUI->pinOutputValue->value());
                sendRawBufferToMachine(gcode.toLatin1());
            } else {
                auto gcode = QString("$L4 Z4 P%1$")
                                 .arg(pin_number);
                sendRawBufferToMachine(gcode.toLatin1());
            }
        });
}

static bool isValidDelimiter(char c) {
    constexpr auto DELIMITERS = std::array<char, 2>{ '#', '$' };
    return std::find(DELIMITERS.begin(), DELIMITERS.end(), c) != DELIMITERS.end();
}

void QTester::interpretLineFromMachine(QByteArray bytes) {
    qDebug() << "received: " << bytes;

    if (bytes.isEmpty())
        return;

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
            // mUI->logSerial->insertHtml(html);
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
        qDebug() << "enviados " << bytes.size() << "bytes\n"
                 << bytes;
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

        mPort->setBaudRate(115200);
        if (not mPort->open(QSerialPort::ReadWrite)) {
            mUI->statusBar->showMessage("failed to open valid port");
        } else {
            mPort->setDataTerminalReady(true);
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
