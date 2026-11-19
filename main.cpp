#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QSocketNotifier>
#include <QTextCursor>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QColor>
#include <KWindowEffects>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

#ifndef APP_ID
#define APP_ID "com.okienko.SerialTerminal"
#endif

namespace {

bool isPolishLocale() {
    static const bool polish = (QLocale::system().language() == QLocale::Polish);
    return polish;
}

QString trText(const char* en, const char* pl) {
    return QString::fromUtf8(isPolishLocale() ? pl : en);
}

struct BaudOption {
    int rate;
    speed_t speed;
};

constexpr BaudOption kBaudOptions[] = {
    {300, B300},
    {1200, B1200},
    {2400, B2400},
    {4800, B4800},
    {9600, B9600},
    {19200, B19200},
    {38400, B38400},
    {57600, B57600},
    {115200, B115200},
    {230400, B230400},
    {460800, B460800},
    {921600, B921600},
};

QStringList detectSerialPorts() {
    QStringList ports;
    QDir sysTty("/sys/class/tty");
    const QStringList entries = sysTty.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QString& name : entries) {
        const QString basePath = sysTty.filePath(name);
        if (!QFileInfo::exists(basePath + "/device")) {
            continue;
        }

        // Skip unconfigured 8250 serial slots (type == 0 / PORT_UNKNOWN)
        QFile typeFile(basePath + "/type");
        if (typeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (typeFile.readAll().trimmed() == "0") {
                continue;
            }
        }

        const QString devPath = "/dev/" + name;
        if (QFileInfo::exists(devPath)) {
            ports.append(devPath);
        }
    }

    return ports;
}

bool configureSerialPort(int fd, speed_t baudSpeed, QString* errorOut) {
    struct termios tty {};
    if (::tcgetattr(fd, &tty) != 0) {
        if (errorOut) {
            *errorOut = trText("Failed to read serial settings: %1", "Nie udało się odczytać ustawień portu: %1")
                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    ::cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    ::cfsetispeed(&tty, baudSpeed);
    ::cfsetospeed(&tty, baudSpeed);

    if (::tcsetattr(fd, TCSANOW, &tty) != 0) {
        if (errorOut) {
            *errorOut = trText("Failed to configure serial port: %1", "Nie udało się skonfigurować portu szeregowego: %1")
                            .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    return true;
}

int openSerialPort(const QString& portPath, speed_t baudSpeed, QString* errorOut) {
    const QByteArray devBytes = portPath.toLocal8Bit();
    int fd = ::open(devBytes.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (errorOut) {
            *errorOut = trText("Failed to open %1: %2", "Nie udało się otworzyć %1: %2")
                            .arg(portPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return -1;
    }

    if (!configureSerialPort(fd, baudSpeed, errorOut)) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool writeAllSerial(int fd, const QByteArray& payload, QString* errorOut) {
    const char* data = payload.constData();
    qsizetype total = payload.size();
    qsizetype written = 0;

    while (written < total) {
        ssize_t n = ::write(fd, data + written, static_cast<size_t>(total - written));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd {};
                pfd.fd = fd;
                pfd.events = POLLOUT;
                int pr = ::poll(&pfd, 1, 2000);
                if (pr > 0) {
                    continue;
                }
                if (errorOut) {
                    *errorOut = (pr == 0)
                                    ? trText("Timed out waiting to write to serial port.",
                                             "Przekroczono czas oczekiwania na zapis do portu szeregowego.")
                                    : trText("Poll error: %1", "Błąd poll: %1")
                                          .arg(QString::fromLocal8Bit(std::strerror(errno)));
                }
                return false;
            }
            if (errorOut) {
                *errorOut = trText("Write error: %1", "Błąd zapisu: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno)));
            }
            return false;
        }
        written += n;
    }

    return true;
}

} // namespace

class TranslucentWindow : public QMainWindow {
public:
    explicit TranslucentWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle(trText("Serial Terminal", "Terminal szeregowy"));
        resize(800, 600);

        // Enable native Wayland per-pixel translucency
        setAttribute(Qt::WA_TranslucentBackground, true);

        // Ensure a native QWindow handle exists so we can attach KWin blur
        winId();
        if (windowHandle()) {
            KWindowEffects::enableBlurBehind(windowHandle(), true);
        }

        // System native menu bar
        QMenu* fileMenu = menuBar()->addMenu(trText("&File", "&Plik"));
        QAction* saveAction = fileMenu->addAction(
            QIcon::fromTheme("document-save"),
            trText("&Save Output...", "&Zapisz wyjście...")
        );
        saveAction->setShortcut(QKeySequence::Save);
        connect(saveAction, &QAction::triggered, this, [this]() {
            saveOutput();
        });
        fileMenu->addSeparator();
        QAction* quitAction = fileMenu->addAction(
            QIcon::fromTheme("application-exit"),
            trText("&Quit", "Za&kończ")
        );
        quitAction->setShortcut(QKeySequence::Quit);
        connect(quitAction, &QAction::triggered, this, &QWidget::close);

        // Serial menu with Port and Baud Rate submenus
        QMenu* serialMenu = menuBar()->addMenu(trText("&Transmission", "&Transmisja"));
        serialMenu->setMinimumWidth(400);
        m_portMenu = serialMenu->addMenu(trText("&Port", "&Port"));
        m_portMenu->setMinimumWidth(220);
        m_portActionGroup = new QActionGroup(this);
        m_portActionGroup->setExclusive(true);
        connect(m_portMenu, &QMenu::aboutToShow, this, [this]() {
            populatePortMenu();
        });

        QMenu* baudMenu = serialMenu->addMenu(trText("&Baud Rate   ", "&Prędkość transmisji   "));
        baudMenu->setMinimumWidth(140);
        m_baudActionGroup = new QActionGroup(this);
        m_baudActionGroup->setExclusive(true);
        for (int i = 0; i < static_cast<int>(std::size(kBaudOptions)); ++i) {
            QAction* act = baudMenu->addAction(QString::number(kBaudOptions[i].rate));
            act->setCheckable(true);
            act->setData(i);
            m_baudActionGroup->addAction(act);
            if (kBaudOptions[i].rate == 115200) {
                act->setChecked(true);
                m_selectedBaudIndex = i;
            }
            connect(act, &QAction::triggered, this, [this, i]() {
                m_selectedBaudIndex = i;
                updateInfoLabel();
                if (m_serialFd >= 0) {
                    QString error;
                    if (!configureSerialPort(m_serialFd, kBaudOptions[m_selectedBaudIndex].speed, &error)) {
                        QMessageBox::warning(
                            this,
                            trText("Baud Rate Change Failed", "Nie udało się zmienić prędkości transmisji"),
                            error
                        );
                    }
                }
            });
        }

        QMenu* helpMenu = menuBar()->addMenu(trText("&Help", "Pomo&c"));
        QAction* aboutAction = helpMenu->addAction(
            QIcon::fromTheme("help-about"),
            trText("&About", "&O programie")
        );
        connect(aboutAction, &QAction::triggered, this, [this]() {
            QMessageBox::about(
                this,
                trText("About Serial Terminal", "O programie Terminal szeregowy"),
                trText(
                    "<b>Serial Terminal</b> %1<br><br>"
                    "A program for sending and receiving serial data.<br><br>"
                    "Licensed under the GNU GPL v3.0.",
                    "<b>Terminal szeregowy</b> %1<br><br>"
                    "Program do wysyłania i odbierania danych przez port szeregowy.<br><br>"
                    "Licencja: GNU GPL v3.0."
                ).arg(QStringLiteral(APP_VERSION))
            );
        });

        // Pick initial serial port if one is detected
        const QStringList initialPorts = detectSerialPorts();
        if (!initialPorts.isEmpty()) {
            m_selectedPort = initialPorts.first();
        }
        populatePortMenu();

        // Central widget: only received text box, input field, Send button, and Start/Stop button
        auto* central = new QWidget(this);
        setCentralWidget(central);

        m_receiveBox = new QPlainTextEdit(central);
        m_receiveBox->setReadOnly(true);
        m_receiveBox->setPlaceholderText(
            trText("Received serial data will appear here...", "Odebrane dane z portu szeregowego pojawią się tutaj...")
        );

        m_textInput = new QLineEdit(central);
        m_textInput->setPlaceholderText(trText("Enter text to send...", "Wpisz tekst do wysłania..."));

        m_sendButton = new QPushButton(trText("Send", "Wyślij"), central);
        m_sendButton->setDefault(true);
        m_sendButton->setEnabled(false);

        m_toggleButton = new QPushButton(central);
        updateToggleButtonStyle(false);
        const QFontMetrics fm(m_toggleButton->font());
        const int toggleWidth = std::max(
            96,
            std::max(
                fm.horizontalAdvance(trText("Start", "Uruchom")),
                fm.horizontalAdvance(trText("Stop", "Zatrzymaj"))
            ) + 40
        );
        m_toggleButton->setFixedWidth(toggleWidth);

        connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
            if (m_serialFd >= 0) {
                stopSerial();
            } else {
                startSerial();
            }
        });

        connect(m_sendButton, &QPushButton::clicked, this, [this]() {
            handleSend();
        });
        connect(m_textInput, &QLineEdit::returnPressed, this, [this]() {
            handleSend();
        });

        auto* inputRow = new QHBoxLayout();
        inputRow->setSpacing(8);
        inputRow->addWidget(m_textInput, 1);
        inputRow->addWidget(m_sendButton);
        inputRow->addWidget(m_toggleButton);

        m_infoLabel = new QLabel(central);
        m_infoLabel->setStyleSheet("QLabel { font-size: 11px; color: rgba(255, 255, 255, 180); }");
        updateInfoLabel();

        auto* mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(16, 16, 16, 8);
        mainLayout->setSpacing(10);
        mainLayout->addWidget(m_receiveBox, 1);
        mainLayout->addLayout(inputRow);
        mainLayout->addWidget(m_infoLabel, 0, Qt::AlignLeft);
    }

    ~TranslucentWindow() override {
        stopSerial();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        // Fixed #232627 background at 70% opacity (alpha = 179 / 255)
        painter.fillRect(rect(), QColor(0x23, 0x26, 0x27, 179));
    }

    void showEvent(QShowEvent* event) override {
        QMainWindow::showEvent(event);
        if (windowHandle()) {
            KWindowEffects::enableBlurBehind(windowHandle(), true);
        }
    }

private:
    void updateToggleButtonStyle(bool running) {
        if (running) {
            m_toggleButton->setText(trText("Stop", "Zatrzymaj"));
            m_toggleButton->setStyleSheet(
                "QPushButton {"
                "  background-color: #c62828;"
                "  color: white;"
                "  font-weight: bold;"
                "  padding: 5px 14px;"
                "  border: none;"
                "  border-radius: 4px;"
                "}"
                "QPushButton:hover { background-color: #d32f2f; }"
                "QPushButton:pressed { background-color: #b71c1c; }"
            );
        } else {
            m_toggleButton->setText(trText("Start", "Uruchom"));
            m_toggleButton->setStyleSheet(
                "QPushButton {"
                "  background-color: #2e7d32;"
                "  color: white;"
                "  font-weight: bold;"
                "  padding: 5px 14px;"
                "  border: none;"
                "  border-radius: 4px;"
                "}"
                "QPushButton:hover { background-color: #388e3c; }"
                "QPushButton:pressed { background-color: #1b5e20; }"
            );
        }
    }

    void populatePortMenu() {
        m_portMenu->clear();
        for (QAction* act : m_portActionGroup->actions()) {
            m_portActionGroup->removeAction(act);
        }

        QStringList ports = detectSerialPorts();
        if (!m_selectedPort.isEmpty() && !ports.contains(m_selectedPort)) {
            ports.prepend(m_selectedPort);
        }

        if (ports.isEmpty()) {
            QAction* noneAction = m_portMenu->addAction(
                trText("No serial ports detected", "Nie wykryto portów szeregowych")
            );
            noneAction->setEnabled(false);
        } else {
            for (const QString& port : ports) {
                QAction* act = m_portMenu->addAction(port);
                act->setCheckable(true);
                act->setChecked(port == m_selectedPort);
                m_portActionGroup->addAction(act);
                connect(act, &QAction::triggered, this, [this, port]() {
                    selectPort(port);
                });
            }
        }

        m_portMenu->addSeparator();
        QAction* customAction = m_portMenu->addAction(trText("Custom Port...", "Własny port..."));
        connect(customAction, &QAction::triggered, this, [this]() {
            bool ok = false;
            const QString input = QInputDialog::getText(
                this,
                trText("Custom Serial Port", "Własny port szeregowy"),
                trText(
                    "Enter serial port path (e.g. /dev/ttyUSB0 or /dev/pts/2):",
                    "Podaj ścieżkę portu szeregowego (np. /dev/ttyUSB0 lub /dev/pts/2):"
                ),
                QLineEdit::Normal,
                m_selectedPort,
                &ok
            );
            if (ok && !input.trimmed().isEmpty()) {
                selectPort(input.trimmed());
            }
        });
    }

    void updateInfoLabel() {
        if (!m_infoLabel) {
            return;
        }
        const int baudRate = kBaudOptions[m_selectedBaudIndex].rate;
        if (m_selectedPort.isEmpty()) {
            m_infoLabel->setText(
                trText("No port selected at %1bps", "Nie wybrano portu przy %1bps").arg(baudRate)
            );
        } else {
            m_infoLabel->setText(
                trText("%1 at %2bps", "%1 przy %2bps").arg(m_selectedPort).arg(baudRate)
            );
        }
    }

    void selectPort(const QString& port) {
        const bool wasRunning = (m_serialFd >= 0);
        if (port == m_selectedPort) {
            return;
        }
        m_selectedPort = port;
        updateInfoLabel();
        if (wasRunning) {
            stopSerial();
            startSerial();
        }
    }

    void startSerial() {
        if (m_selectedPort.isEmpty()) {
            QMessageBox::warning(
                this,
                trText("No Serial Port Selected", "Nie wybrano portu szeregowego"),
                trText(
                    "Please select a serial port in the Transmission menu first.",
                    "Najpierw wybierz port szeregowy w menu Transmisja."
                )
            );
            return;
        }

        QString error;
        const BaudOption& baud = kBaudOptions[m_selectedBaudIndex];
        int fd = openSerialPort(m_selectedPort, baud.speed, &error);
        if (fd < 0) {
            QMessageBox::warning(
                this,
                trText("Failed to Start Serial Port", "Nie udało się uruchomić portu szeregowego"),
                error
            );
            return;
        }

        m_serialFd = fd;
        m_readNotifier = new QSocketNotifier(m_serialFd, QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this, [this]() {
            handleSerialRead();
        });

        m_sendButton->setEnabled(true);
        updateToggleButtonStyle(true);
    }

    void stopSerial() {
        if (m_readNotifier) {
            m_readNotifier->setEnabled(false);
            m_readNotifier->deleteLater();
            m_readNotifier = nullptr;
        }
        if (m_serialFd >= 0) {
            ::close(m_serialFd);
            m_serialFd = -1;
        }
        if (m_sendButton) {
            m_sendButton->setEnabled(false);
        }
        if (m_toggleButton) {
            updateToggleButtonStyle(false);
        }
    }

    void handleSerialRead() {
        if (m_serialFd < 0) {
            return;
        }

        char buffer[1024];
        QByteArray received;
        bool disconnected = false;
        QString readError;

        while (true) {
            ssize_t n = ::read(m_serialFd, buffer, sizeof(buffer));
            if (n > 0) {
                received.append(buffer, static_cast<qsizetype>(n));
            } else if (n == 0) {
                struct pollfd pfd {};
                pfd.fd = m_serialFd;
                pfd.events = POLLIN;
                if (::poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
                    disconnected = true;
                }
                break;
            } else {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                readError = QString::fromLocal8Bit(std::strerror(errno));
                disconnected = true;
                break;
            }
        }

        if (!received.isEmpty()) {
            QTextCursor cursor = m_receiveBox->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertText(QString::fromUtf8(received));
            m_receiveBox->setTextCursor(cursor);
            m_receiveBox->ensureCursorVisible();
        }

        if (disconnected) {
            stopSerial();
            if (!readError.isEmpty()) {
                QMessageBox::warning(
                    this,
                    trText("Serial Read Error", "Błąd odczytu z portu szeregowego"),
                    trText("Read error: %1", "Błąd odczytu: %1").arg(readError)
                );
            } else {
                QMessageBox::warning(
                    this,
                    trText("Serial Disconnected", "Rozłączono port szeregowy"),
                    trText("The serial port was closed or disconnected.", "Port szeregowy został zamknięty lub odłączony.")
                );
            }
        }
    }

    void saveOutput() {
        const QString filePath = QFileDialog::getSaveFileName(
            this,
            trText("Save Output", "Zapisz wyjście"),
            "serial-output.txt",
            trText(
                "Text Files (*.txt);;Log Files (*.log);;All Files (*)",
                "Pliki tekstowe (*.txt);;Pliki dziennika (*.log);;Wszystkie pliki (*)"
            )
        );
        if (filePath.isEmpty()) {
            return;
        }

        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(
                this,
                trText("Save Output Failed", "Nie udało się zapisać wyjścia"),
                trText("Could not open %1 for writing: %2", "Nie można otworzyć %1 do zapisu: %2")
                    .arg(filePath, file.errorString())
            );
            return;
        }

        const QByteArray data = m_receiveBox->toPlainText().toUtf8();
        if (file.write(data) != data.size()) {
            QMessageBox::warning(
                this,
                trText("Save Output Failed", "Nie udało się zapisać wyjścia"),
                trText("Failed to write all data to %1: %2", "Nie udało się zapisać wszystkich danych do %1: %2")
                    .arg(filePath, file.errorString())
            );
        }
    }

    void handleSend() {
        if (m_textInput->text().trimmed() == "/clear") {
            m_receiveBox->clear();
            m_textInput->clear();
            return;
        }

        if (m_serialFd < 0) {
            QMessageBox::warning(
                this,
                trText("Serial Port Stopped", "Port szeregowy zatrzymany"),
                trText("Start the serial port before sending data.", "Uruchom port szeregowy przed wysłaniem danych.")
            );
            return;
        }

        const QByteArray payload = m_textInput->text().toUtf8();
        if (payload.isEmpty()) {
            return;
        }

        QString error;
        if (!writeAllSerial(m_serialFd, payload, &error)) {
            QMessageBox::warning(
                this,
                trText("Serial Send Failed", "Nie udało się wysłać danych"),
                error
            );
        } else {
            m_textInput->clear();
        }
    }

    QMenu* m_portMenu = nullptr;
    QActionGroup* m_portActionGroup = nullptr;
    QActionGroup* m_baudActionGroup = nullptr;

    QString m_selectedPort;
    int m_selectedBaudIndex = 0;

    int m_serialFd = -1;
    QSocketNotifier* m_readNotifier = nullptr;

    QPlainTextEdit* m_receiveBox = nullptr;
    QLineEdit* m_textInput = nullptr;
    QPushButton* m_sendButton = nullptr;
    QPushButton* m_toggleButton = nullptr;
    QLabel* m_infoLabel = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SerialTerminal"));
    QApplication::setApplicationDisplayName(trText("Serial Terminal", "Terminal szeregowy"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral(APP_ID));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral(APP_ID)));

    TranslucentWindow window;
    window.show();

    return app.exec();
}
