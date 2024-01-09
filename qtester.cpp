#include "qtester.h"
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QBluetoothLocalDevice>
#include <QBluetoothSocket>
#include <QDataStream>
#include <QFile>
#include <QKeyEvent>
#include <QLatin1StringView>
#include <QScrollBar>
#include "ui_qtester.h"
#include <algorithm>

constexpr auto MAX_BUFFER_SIZE = qsizetype(256);

QTester::QTester(QWidget* parent)
    : QMainWindow(parent)
    , mUI(std::make_unique<Ui::QTester>())
    , mDiscoveryAgent(new QBluetoothDeviceDiscoveryAgent(this))
    , mLocalDevice(new QBluetoothLocalDevice(this))
    , mSocket(new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol, this))
    , mFirmwareFile(nullptr)
    , mFirmwareStream(nullptr) {
    mUI->setupUi(this);
    mUI->logSerial->ensureCursorVisible();
    mUI->userInput->installEventFilter(this);

    if (!mLocalDevice->isValid()) {
        mUI->statusBar->showMessage("Bluetooth não está disponível :(");
        return;
    }

    mLocalDevice->powerOn();

    connect(mDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, [&](const QBluetoothDeviceInfo& info) {
        if (info.name() == "LUCAS_BT") {
            mUI->statusBar->showMessage("Conectando...");

            mDiscoveryAgent->stop();
            mSocket->connectToService(info.address(), QBluetoothUuid::ServiceClassUuid::SerialPort);
        }
    });

    connect(mDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this, [&](QBluetoothDeviceDiscoveryAgent::Error) {
        mUI->statusBar->showMessage(QString("Erro na descoberta - %0 [%1]")
                                        .arg(mDiscoveryAgent->errorString())
                                        .arg(static_cast<int>(mDiscoveryAgent->error())));
        // error when discovering devices, could be because of a sudden disconnection
        // for now just ignore it and search again
        // FIXME: handle the error?
        searchForDevice();
    });

    connect(mDiscoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, [&] {
        // the search has finished!
        // if we are connected or in the process of connecting don't do anything
        // otherwise search again
        switch (mSocket->state()) {
        case QBluetoothSocket::SocketState::ConnectedState:
        case QBluetoothSocket::SocketState::ConnectingState:
            qDebug() << "discovery finshed";
            break;
        default:
            qDebug() << "device not found, searching again";
            searchForDevice();
        }
    });

    connect(mSocket, &QBluetoothSocket::connected, this, [&] {
        mUI->statusBar->showMessage("Conectado - " + mSocket->peerName());
    });

    connect(mSocket, &QBluetoothSocket::disconnected, this, [&] {
        mUI->statusBar->showMessage("Desconectado");
        searchForDevice();
    });

    connect(mSocket, &QBluetoothSocket::errorOccurred, this, [&](QBluetoothSocket::SocketError error) {
        mUI->statusBar->showMessage(QString("Erro na socket - %0 [%1]").arg(mSocket->errorString()).arg((int)mSocket->error()));
        searchForDevice();
    });

    connect(mSocket, &QBluetoothSocket::readyRead, this, &QTester::onSocketReady);

    connect(mUI->clearLogSerial, &QPushButton::clicked, mUI->logSerial, &QTextEdit::clear);

    const auto updatePumpText = [this](bool state) {
        mUI->testPump->setText((state ? "Desligar" : "Ligar") + QString(" Bomba"));
    };

    updatePumpText(false);
    connect(mUI->testPump, &QPushButton::clicked, this, [this, updatePumpText] {
        static bool s_state = true;

        auto gcode = QString("$L5 T0 S%1 V%2$").arg(s_state).arg(mUI->pumpDigitalValue->value());
        qDebug() << gcode;
        sendRawBufferToMachine(gcode.toLatin1());
        updatePumpText(s_state);

        s_state = not s_state;
    });

    static auto resistances = std::array{ mUI->testResistance1, mUI->testResistance2, mUI->testResistance3 };
    static auto resistancesPins = std::array{ 69, 16, 0 };
    static auto resistancesPwm = std::array{ mUI->testResistancePwm1, mUI->testResistancePwm2, mUI->testResistancePwm3 };
    static auto resistancesNames = std::array{ QString("TH0"), QString("TH1"), QString("Bed") };

    const auto updateResistanceText = [this](QPushButton* resistance, QString name, bool state) {
        resistance->setText((state ? "Desligar " : "Ligar ") + name);
    };

    for (int i = 0; i < 3; ++i) {
        updateResistanceText(resistances[i], resistancesNames[i], false);
        connect(resistances[i], &QPushButton::clicked, this, [&, index = i, state = true]() mutable {
            auto gcode = QString("$L6 P%1 M1 V%2 W$").arg(resistancesPins[index]).arg(state ? resistancesPwm[index]->value() : 0);
            sendRawBufferToMachine(gcode.toLatin1());
            updateResistanceText(resistances[index], resistancesNames[index], state);

            state = not state;
        });
    }

    connect(mUI->testBeeper, &QPushButton::pressed, this, [this] { sendRawBufferToMachine(QString("$L5 T6 S1 V%1$").arg(mUI->beeperFrequency->value()).toLatin1()); });

    connect(mUI->testBeeper, &QPushButton::released, this, [this] { sendRawBufferToMachine("$L5 T6 S0$"); });

    connect(mUI->logFlowSensor, &QCheckBox::clicked, this, [this] {
        auto gcode = QString("$L5 T4 S%1$").arg(int(mUI->logFlowSensor->isChecked()));
        sendRawBufferToMachine(gcode.toLatin1());
    });

    connect(mUI->logTemperature, &QCheckBox::clicked, this, [this] {
        auto gcode = QString("$L5 T5 S%1$").arg(int(mUI->logTemperature->isChecked()));
        sendRawBufferToMachine(gcode.toLatin1());
    });

    connect(mUI->heatUpWater, &QCheckBox::clicked, this, [this] {
        auto gcode = QString("$L5 T7 S%1$").arg(int(mUI->heatUpWater->isChecked()));
        sendRawBufferToMachine(gcode.toLatin1());
    });

    connect(mUI->xAxisLeft, &QToolButton::clicked, this, [this] { sendRawBufferToMachine("$G0 X-1$"); });

    connect(mUI->xAxisRight, &QToolButton::clicked, this, [this] { sendRawBufferToMachine("$G0 X1$"); });

    connect(mUI->yAxisDown, &QToolButton::clicked, this, [this] { sendRawBufferToMachine("$G0 Y-1$"); });

    connect(mUI->yAxisUp, &QToolButton::clicked, this, [this] { sendRawBufferToMachine("$G0 Y1$"); });

    connect(mUI->home, &QPushButton::clicked, this, [this] { sendRawBufferToMachine("$G28 XY$"); });

    const auto changeLedText = [this](int index) {
        auto str1 = QString("Testar LED %1").arg(index + 1);
        auto str2 = QString("Testar PowerLED %1").arg(index + 1);
        auto str3 = QString("Viajar Para %1").arg(index + 1);
        mUI->testButtonLed->setText(str1);
        mUI->testPowerLed->setText(str2);
        mUI->travelToStation->setText(str3);
    };

    changeLedText(mUI->selectedStation->currentIndex());

    connect(mUI->selectedStation, &QComboBox::currentIndexChanged, this, changeLedText);

    connect(mUI->testButtonLed, &QPushButton::clicked, this, [this] {
        auto gcode = QString("$L5 T2 V%1$").arg(mUI->selectedStation->currentIndex());
        sendRawBufferToMachine(gcode.toLatin1());
    });

    connect(mUI->testPowerLed, &QPushButton::clicked, this, [this] {
        auto gcode = QString("$L5 T3 V%1$").arg(mUI->selectedStation->currentIndex());
        sendRawBufferToMachine(gcode.toLatin1());
    });

    connect(mUI->travelToStation, &QPushButton::clicked, this, [this] {
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
    connect(mUI->pinCfg, &QComboBox::currentIndexChanged, this, changePinCommandText);

    connect(mUI->sendPinCommand, &QPushButton::clicked, this, [this] {
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

#ifndef ANDROID
    mUI->flexButton->setText("Atualizar Firmware");
    connect(mUI->flexButton, &QPushButton::clicked, this, &QTester::startFirmwareUpdate);
#else
    mUI->flexButton->setText("Enviar Comando");
    connect(mUI->flexButton, &QPushButton::clicked, this, &QTester::sendUserInput);
#endif

    connect(mUI->leaveTesterMode, &QPushButton::clicked, this, [this] { sendRawBufferToMachine("$L4 K0$"); });

    mUI->statusBar->showMessage("Procurando...");
    searchForDevice();
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
            const auto color = QApplication::palette().text().color().name(QColor::HexRgb);
            if (bytes.startsWith("ERRO:")) {
                const auto html = QString(R"(<p style="color:crimson;">%1<span style="color:%2;">%3<br></p>)").arg(bytes.first(5), color, bytes.sliced(5));
                QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
            } else if (isalpha(bytes[0]) and bytes[1] == ':') {
                const auto html = QString(R"(<p style="color:skyblue;">%1<span style="color:%2;">%3<br></p>)").arg(bytes.first(2), color, bytes.sliced(2));
                QMetaObject::invokeMethod(mUI->logSerial, "insertHtml", Q_ARG(QString, html));
            } else {
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

    if (mSocket->state() != QBluetoothSocket::SocketState::ConnectedState)
        return;

    const auto sentBytes = mSocket->write(bytes);
    if (sentBytes == -1) {
        qDebug() << "falha ao enviar - [" << mSocket->errorString() << "]";
    } else {
        qDebug() << "enviados " << bytes.size() << "bytes\n"
                 << bytes;
    }
}

void QTester::startFirmwareUpdate() {
    if (mSocket->state() != QBluetoothSocket::SocketState::ConnectedState)
        return;

    constexpr auto FIRMWARE_PATH = "Robin_nano_V3.bin";
    mFirmwareFile = new QFile(FIRMWARE_PATH, this);
    if (mFirmwareFile->error()) {
        qDebug() << "ERRO: " << mFirmwareFile->errorString();
        delete mFirmwareFile;
        return;
    }

    mFirmwareFile->open(QFile::ReadOnly);
    mFirmwareStream = new QDataStream(mFirmwareFile);

    auto command = QString("#{\"cmdFirmwareUpdate\":%1}#").arg(QString::number(mFirmwareFile->size())).toLatin1();
    sendRawBufferToMachine(command);
}

void QTester::continueStreamingNewFirmware() {
    QByteArray buffer = {};
    buffer.reserve(MAX_BUFFER_SIZE);

    const auto bytesRead = mFirmwareStream->readRawData(buffer.data(), MAX_BUFFER_SIZE);
    if (bytesRead == -1) {
        delete mFirmwareStream;
        delete mFirmwareFile;
        qDebug() << "erro no envio";
        return;
    }

    buffer.resize(bytesRead);
    sendRawBufferToMachine(buffer);

    if (mFirmwareStream->atEnd()) {
        delete mFirmwareStream;
        delete mFirmwareFile;
        qDebug() << "fim do envio";
    }
}

void QTester::onSocketReady() {
    while (mSocket->canReadLine())
        interpretLineFromMachine(mSocket->readLine().simplified());
}

void QTester::searchForDevice() {
    if (mDiscoveryAgent->isActive())
        mDiscoveryAgent->stop();
    mDiscoveryAgent->start(QBluetoothDeviceDiscoveryAgent::ClassicMethod);
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

QTester::~QTester() = default;
