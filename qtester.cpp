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

#ifdef ANDROID
    #include <QtCore/qjniobject.h>
#endif

constexpr auto FIRMWARE_PATH = "Robin_nano_V3.bin";
constexpr auto MAX_BUFFER_SIZE = qsizetype(64);

QTester::QTester(QWidget* parent)
    : QMainWindow(parent)
    , mTimer(std::make_unique<QTimer>(this))
    , mUI(std::make_unique<Ui::QTester>())
    , mFirmwareFile(nullptr)
    , mFirmwareStream(nullptr) {
    if (s_instance)
        std::exit(1);

    s_instance = this;
    mUI->setupUi(this);
    mUI->statusBar->showMessage("Procurando...");
    mUI->logSerial->ensureCursorVisible();

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

    const auto updatePumpText = [this] (bool state) {
        mUI->testPump->setText((state ? "Desligar" : "Ligar") + QString(" Bomba"));
    };

    updatePumpText(false);
    connect(
        mUI->testPump,
        &QPushButton::clicked,
        this,
        [this, updatePumpText] {
            static bool s_state = true;

            auto gcode = QString("$L5 T0 S%1 V%2$").arg(s_state).arg(mUI->pumpDigitalValue->value());
            qDebug() << gcode;
            sendRawBufferToMachine(gcode.toLatin1());
            updatePumpText(s_state);

            s_state = not s_state;
        });

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
        mUI->heatUpWater,
        &QCheckBox::clicked,
        this,
        [this] {
            auto gcode = QString("$L5 T7 S%1$").arg(int(mUI->heatUpWater->isChecked()));
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

    connect(
        mUI->home,
        &QPushButton::clicked,
        this,
        [this] {
            auto gcode = QString("$G28 XY$");
            sendRawBufferToMachine(gcode.toLatin1());
        });

    const auto changeLedText = [this](int index) {
        auto str1 = QString("Testar LED %1").arg(index + 1);
        auto str2 = QString("Testar PowerLED %1").arg(index + 1);
        auto str3 = QString("Viajar Para %1").arg(index + 1);
        mUI->testButtonLed->setText(str1);
        mUI->testPowerLed->setText(str2);
        mUI->travelToStation->setText(str3);
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

    connect(
        mUI->travelToStation,
        &QPushButton::clicked,
        this,
        [this] {
            auto gcode = QString("$L3 N%1$").arg(mUI->selectedStation->currentIndex());
            sendRawBufferToMachine(gcode.toLatin1());
        });

    const auto changePinCommandText = [this](int index) {
        bool is_output = index == 0x1;
        mUI->pinOutputLabel->setEnabled(is_output);
        mUI->pinOutputValue->setEnabled(is_output);
        mUI->sendPinCommand->setText(is_output ? "Enviar Valor Digital" : "Ler Valor Digital");
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
                auto gcode = QString("$L6 P%1 M%2 V%3 W$")
                                 .arg(pin_number)
                                 .arg(mUI->pinCfg->currentIndex())
                                 .arg(mUI->pinOutputValue->value());
                sendRawBufferToMachine(gcode.toLatin1());
            } else {
                auto gcode = QString("$L6 P%1 M%2 R$")
                                 .arg(pin_number)
                                 .arg(mUI->pinCfg->currentIndex());
                sendRawBufferToMachine(gcode.toLatin1());
            }
        });

#if ANDROID
    mUI->flexButton->setText("Enviar Comando");
    connect(
        mUI->flexButton,
        &QPushButton::clicked,
        this,
        &QTester::sendUserInput);
#else
    mUI->flexButton->setText("Atualizar Firmware");
    connect(
        mUI->flexButton,
        &QPushButton::clicked,
        this,
        &QTester::startFirmwareUpdate);

#endif

    connect(
        mUI->leaveTesterMode,
        &QPushButton::clicked,
        this,
        [this] {
            sendRawBufferToMachine("$L4 K0$");
        });
}

static bool isValidDelimiter(char c) {
    return c == '#' || c == '$';
}

void QTester::interpretLineFromMachine(QByteArray bytes) {
    qDebug() << "received: " << bytes;

    if (bytes.isEmpty())
        return;

    if (bytes == R"(#{"infoOther":{"okToReceive":true}}#)") { // ne segredo
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
                QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
            } else if (isalpha(bytes[0]) and bytes[1] == ':') {
                const auto html = QString(R"(<p style="color:skyblue;">%1<span style="color:white;">%2<br></p>)").arg(bytes.first(2), bytes.sliced(2));
                QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
            } else {
                const auto color = QApplication::palette().text().color().name(QColor::HexRgb);
                qDebug() << color;
                const auto html = QString(R"(<p style="color:%1;">%2<br></p>)").arg(color).arg(bytes);
                QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
            }
        } else {
            const auto html = QString(R"(<p style="color:gray;">%1<br></p>)").arg(bytes);
            QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
        }

        if (shouldScroll)
            QMetaObject::invokeMethod(scrollbar, "setValue", Q_ARG(int, scrollbar->maximum()));
    }
}

void QTester::sendUserInput() {
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
    sendRawBufferToMachine(slicedBuffer);
    buffer.remove(0, sliceSize);
}

void QTester::sendRawBufferToMachine(QByteArray bytes) {
    if (bytes.isEmpty())
        return;

    if (not mFirmwareStream)
        if (bytes.back() == '$' and bytes[bytes.size() - 2] != '\0')
            bytes.insert(bytes.size() - 1, '\0');

#ifdef ANDROID
    QJniEnvironment env;
    jbyteArray buffer = env->NewByteArray(bytes.size());
    if (not buffer)
        return;

    env->SetByteArrayRegion(buffer, 0, bytes.size(), (jbyte const*)bytes.data());

    QJniObject::callStaticMethod<void>("com/lucas/tester/Tester", "send", "([B)V", buffer);

    env->DeleteLocalRef(buffer);
#else
    if (not mPort or not mPort->isOpen())
        return;

    const auto sentBytes = mPort->write(bytes);
    if (sentBytes == -1) {
        qDebug() << "falha ao enviar - [" << mPort->errorString() << "]";
    } else {
        qDebug() << "enviados " << bytes.size() << "bytes\n"
                 << bytes;
    }
#endif
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


#ifdef ANDROID
void QTester::onPortReady(JNIEnv* env, jobject, jbyteArray jdata) {
    static QByteArray buffer = {};

    jsize jdataSize = env->GetArrayLength(jdata);
    if (jdataSize == 0)
        return;

    auto jelements = env->GetByteArrayElements(jdata, nullptr);
    buffer.append((char const*)jelements, jdataSize);
    env->ReleaseByteArrayElements(jdata, jelements, JNI_ABORT);

    qsizetype i = buffer.indexOf('\n');
    while (i != -1) {
        QTester::the().interpretLineFromMachine(buffer.first(i));
        buffer.remove(0, i + 1);
        i = buffer.indexOf('\n');
    }
}
#else
void QTester::onPortReady() {
    if (not mPort || not mPort->isOpen())
        return;

    while (mPort->canReadLine())
        interpretLineFromMachine(mPort->readLine().simplified());
}
#endif

void QTester::tryFetchingNewConnection() {
#ifdef ANDROID
    static bool once = false;
    if (not once) {
        const JNINativeMethod methods[] = { "dataReceived", "([B)V", (void*)&QTester::onPortReady };
        QJniEnvironment env;
        env.registerNativeMethods("com/lucas/tester/TesterListener", methods, 1);
        once = true;
    }

    jboolean arg = false;
    QJniObject::callStaticMethod<void>(
        "com/lucas/tester/Tester",
        "create",
        "(Landroid/content/Context;Z)V",
        QNativeInterface::QAndroidApplication::context(),
        arg);

    QJniObject jobj = QJniObject::getStaticObjectField("com/lucas/tester/Tester", "mSerialPort",
                                                       "Lcom/hoho/android/usbserial/driver/UsbSerialPort;");
    if (jobj.isValid()) {
        if (mUI->statusBar->currentMessage() != "Conectado") {
            mUI->logSerial->insertHtml(R"(<p style="color:pink;">~~NEW~CONNECTION~~<br></p>)");
            mUI->statusBar->showMessage("Conectado");
            sendRawBufferToMachine("$L4 K1$");
        }
    } else {
        mUI->statusBar->showMessage("Desconectado");
    }

#else
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
#endif
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
