#include <adwaita.h>
#include <glib-unix.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <grp.h>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

#ifndef APP_ID
#define APP_ID "io.github.Szaru3.SerialTerminal.Gnome"
#endif

namespace {

bool isPolishLocale() {
    static const bool polish = []() {
        const char* env = std::getenv("LC_ALL");
        if (!env || !*env) env = std::getenv("LC_MESSAGES");
        if (!env || !*env) env = std::getenv("LANG");
        if (!env) return false;
        return std::strncmp(env, "pl", 2) == 0;
    }();
    return polish;
}

const char* trText(const char* en, const char* pl) {
    return isPolishLocale() ? pl : en;
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

bool pathExists(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

std::vector<std::string> detectSerialPorts() {
    std::vector<std::string> ports;
    DIR* dir = ::opendir("/sys/class/tty");
    if (!dir) {
        return ports;
    }

    std::vector<std::string> entries;
    while (struct dirent* ent = ::readdir(dir)) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        entries.emplace_back(ent->d_name);
    }
    ::closedir(dir);

    std::sort(entries.begin(), entries.end());

    for (const std::string& name : entries) {
        const std::string basePath = "/sys/class/tty/" + name;
        if (!pathExists(basePath + "/device")) {
            continue;
        }

        // Skip unconfigured 8250 serial slots (type == 0 / PORT_UNKNOWN)
        std::ifstream typeFile(basePath + "/type");
        if (typeFile.is_open()) {
            std::string typeVal;
            typeFile >> typeVal;
            if (typeVal == "0") {
                continue;
            }
        }

        const std::string devPath = "/dev/" + name;
        if (pathExists(devPath)) {
            ports.push_back(devPath);
        }
    }

    return ports;
}

bool configureSerialPort(int fd, speed_t baudSpeed, std::string* errorOut) {
    struct termios tty {};
    if (::tcgetattr(fd, &tty) != 0) {
        if (errorOut) {
            *errorOut = std::string(trText("Failed to read serial settings: ",
                                           "Nie udało się odczytać ustawień portu: ")) +
                        std::strerror(errno);
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
            *errorOut = std::string(trText("Failed to configure serial port: ",
                                           "Nie udało się skonfigurować portu szeregowego: ")) +
                        std::strerror(errno);
        }
        return false;
    }

    return true;
}

std::string detectSerialGroup(const char* portPath) {
    struct stat st {};
    if (::stat(portPath, &st) == 0) {
        if (struct group* gr = ::getgrgid(st.st_gid); gr && gr->gr_name && std::strcmp(gr->gr_name, "root") != 0) {
            return gr->gr_name;
        }
    }
    if (::getgrnam("uucp") != nullptr) {
        return "uucp";
    }
    return "dialout";
}

int openSerialPort(const std::string& portPath, speed_t baudSpeed, std::string* errorOut) {
    int fd = ::open(portPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        const int openErr = errno;
        if (errorOut) {
            const std::string sysErr = std::strerror(openErr);
            if (openErr == EACCES || openErr == EPERM) {
                const std::string groupName = detectSerialGroup(portPath.c_str());
                if (isPolishLocale()) {
                    *errorOut = "Nie udało się otworzyć " + portPath + ": " + sysErr +
                                "\n\nTwoje konto użytkownika nie ma uprawnień do tego portu szeregowego. "
                                "Dodaj się do grupy '" + groupName +
                                "', a następnie wyloguj i zaloguj ponownie:\n\n"
                                "sudo usermod -aG " + groupName + " \"$USER\"";
                } else {
                    *errorOut = "Failed to open " + portPath + ": " + sysErr +
                                "\n\nYour user account does not have permission to access this serial port. "
                                "Add yourself to the '" + groupName +
                                "' group, then log out and log back in:\n\n"
                                "sudo usermod -aG " + groupName + " \"$USER\"";
                }
            } else {
                *errorOut = std::string(trText("Failed to open ", "Nie udało się otworzyć ")) +
                            portPath + ": " + sysErr;
            }
        }
        return -1;
    }

    if (!configureSerialPort(fd, baudSpeed, errorOut)) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool writeAllSerial(int fd, const std::string& payload, std::string* errorOut) {
    const char* data = payload.data();
    size_t total = payload.size();
    size_t written = 0;

    while (written < total) {
        ssize_t n = ::write(fd, data + written, total - written);
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
                                    : std::string(trText("Poll error: ", "Błąd poll: ")) +
                                          std::strerror(errno);
                }
                return false;
            }
            if (errorOut) {
                *errorOut = std::string(trText("Write error: ", "Błąd zapisu: ")) +
                            std::strerror(errno);
            }
            return false;
        }
        written += static_cast<size_t>(n);
    }

    return true;
}

struct AppState {
    AdwApplication* app = nullptr;
    AdwApplicationWindow* window = nullptr;
    AdwWindowTitle* windowTitle = nullptr;
    GtkMenuButton* portMenuButton = nullptr;
    GtkMenuButton* baudMenuButton = nullptr;
    GtkTextView* receiveView = nullptr;
    GtkTextBuffer* receiveBuffer = nullptr;
    GtkEntry* textInput = nullptr;
    GtkButton* sendButton = nullptr;
    GtkButton* toggleButton = nullptr;

    GSimpleAction* portAction = nullptr;
    GSimpleAction* baudAction = nullptr;

    std::string selectedPort;
    int selectedBaudIndex = 8; // 115200

    int serialFd = -1;
    guint readSourceId = 0;

    void showWarning(const char* heading, const std::string& body) {
        AdwDialog* dialog = adw_alert_dialog_new(heading, body.c_str());
        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", trText("OK", "OK"));
        adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
        adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "ok");
        adw_dialog_present(dialog, GTK_WIDGET(window));
    }

    void updateInfoLabel() {
        const int baudRate = kBaudOptions[selectedBaudIndex].rate;
        std::string text;
        if (selectedPort.empty()) {
            text = std::string(trText("No port selected at ", "Nie wybrano portu przy ")) +
                   std::to_string(baudRate) + "bps";
        } else {
            text = selectedPort + trText(" at ", " przy ") + std::to_string(baudRate) + "bps";
        }
        if (windowTitle) {
            adw_window_title_set_subtitle(windowTitle, text.c_str());
        }
        if (baudMenuButton) {
            std::string baudLabel = std::to_string(baudRate) + " bps";
            gtk_menu_button_set_label(baudMenuButton, baudLabel.c_str());
        }
        if (portMenuButton) {
            if (selectedPort.empty()) {
                gtk_menu_button_set_label(portMenuButton, trText("Port", "Port"));
            } else {
                const auto slash = selectedPort.rfind('/');
                const std::string shortName =
                    (slash != std::string::npos) ? selectedPort.substr(slash + 1) : selectedPort;
                gtk_menu_button_set_label(portMenuButton, shortName.c_str());
            }
        }
    }

    void updateToggleButton(bool running) {
        if (!toggleButton) {
            return;
        }
        if (running) {
            gtk_button_set_label(toggleButton, trText("Stop", "Zatrzymaj"));
            gtk_widget_remove_css_class(GTK_WIDGET(toggleButton), "start-action");
            gtk_widget_add_css_class(GTK_WIDGET(toggleButton), "destructive-action");
        } else {
            gtk_button_set_label(toggleButton, trText("Start", "Uruchom"));
            gtk_widget_remove_css_class(GTK_WIDGET(toggleButton), "destructive-action");
            gtk_widget_add_css_class(GTK_WIDGET(toggleButton), "start-action");
        }
    }

    void rebuildPortMenu() {
        GMenu* menu = g_menu_new();
        GMenu* portsSection = g_menu_new();

        std::vector<std::string> ports = detectSerialPorts();
        if (!selectedPort.empty() &&
            std::find(ports.begin(), ports.end(), selectedPort) == ports.end()) {
            ports.insert(ports.begin(), selectedPort);
        }

        if (ports.empty()) {
            GMenuItem* emptyItem =
                g_menu_item_new(trText("No serial ports detected", "Nie wykryto portów szeregowych"),
                                "win.disabled-placeholder");
            g_menu_append_item(portsSection, emptyItem);
            g_object_unref(emptyItem);
        } else {
            for (const std::string& port : ports) {
                GMenuItem* item = g_menu_item_new(port.c_str(), nullptr);
                g_menu_item_set_action_and_target_value(item, "win.select-port",
                                                        g_variant_new_string(port.c_str()));
                g_menu_append_item(portsSection, item);
                g_object_unref(item);
            }
        }

        g_menu_append_section(menu, nullptr, G_MENU_MODEL(portsSection));
        g_object_unref(portsSection);

        GMenu* customSection = g_menu_new();
        g_menu_append(customSection, trText("Custom Port...", "Własny port..."), "win.custom-port");
        g_menu_append_section(menu, nullptr, G_MENU_MODEL(customSection));
        g_object_unref(customSection);

        gtk_menu_button_set_menu_model(portMenuButton, G_MENU_MODEL(menu));
        g_object_unref(menu);
    }

    std::string settingsFilePath() const {
        char* path = g_build_filename(g_get_user_config_dir(), "serial-terminal", "settings.ini", nullptr);
        std::string result(path ? path : "");
        g_free(path);
        return result;
    }

    void loadSettings() {
        const std::string path = settingsFilePath();
        if (path.empty()) {
            return;
        }
        GKeyFile* keyFile = g_key_file_new();
        if (g_key_file_load_from_file(keyFile, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
            if (char* port = g_key_file_get_string(keyFile, "serial", "port", nullptr)) {
                selectedPort = port;
                g_free(port);
            }
            GError* err = nullptr;
            int savedBaud = g_key_file_get_integer(keyFile, "serial", "baud_rate", &err);
            if (!err) {
                for (int i = 0; i < static_cast<int>(std::size(kBaudOptions)); ++i) {
                    if (kBaudOptions[i].rate == savedBaud) {
                        selectedBaudIndex = i;
                        break;
                    }
                }
            } else {
                g_error_free(err);
            }
        }
        g_key_file_free(keyFile);
    }

    void saveSettings() const {
        const std::string path = settingsFilePath();
        if (path.empty()) {
            return;
        }
        char* dir = g_path_get_dirname(path.c_str());
        if (dir) {
            g_mkdir_with_parents(dir, 0700);
            g_free(dir);
        }
        GKeyFile* keyFile = g_key_file_new();
        g_key_file_set_string(keyFile, "serial", "port", selectedPort.c_str());
        g_key_file_set_integer(keyFile, "serial", "baud_rate", kBaudOptions[selectedBaudIndex].rate);
        g_key_file_save_to_file(keyFile, path.c_str(), nullptr);
        g_key_file_free(keyFile);
    }

    void selectPort(const std::string& port) {
        const bool wasRunning = (serialFd >= 0);
        if (port == selectedPort) {
            return;
        }
        selectedPort = port;
        saveSettings();
        if (portAction) {
            g_simple_action_set_state(portAction, g_variant_new_string(selectedPort.c_str()));
        }
        updateInfoLabel();
        if (wasRunning) {
            stopSerial();
            startSerial();
        }
    }

    static gboolean onSerialReadable(gint /*fd*/, GIOCondition /*cond*/, gpointer userData) {
        auto* self = static_cast<AppState*>(userData);
        return self->handleSerialRead();
    }

    void startSerial() {
        if (selectedPort.empty()) {
            showWarning(
                trText("No Serial Port Selected", "Nie wybrano portu szeregowego"),
                trText("Please select a serial port in the Port menu first.",
                       "Najpierw wybierz port szeregowy w menu Port."));
            return;
        }

        std::string error;
        const BaudOption& baud = kBaudOptions[selectedBaudIndex];
        int fd = openSerialPort(selectedPort, baud.speed, &error);
        if (fd < 0) {
            showWarning(
                trText("Failed to Start Serial Port", "Nie udało się uruchomić portu szeregowego"),
                error);
            return;
        }

        serialFd = fd;
        readSourceId = g_unix_fd_add(
            serialFd,
            static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
            onSerialReadable,
            this);

        gtk_widget_set_sensitive(GTK_WIDGET(sendButton), TRUE);
        updateToggleButton(true);
    }

    void stopSerial() {
        if (readSourceId != 0) {
            g_source_remove(readSourceId);
            readSourceId = 0;
        }
        if (serialFd >= 0) {
            ::close(serialFd);
            serialFd = -1;
        }
        if (sendButton) {
            gtk_widget_set_sensitive(GTK_WIDGET(sendButton), FALSE);
        }
        updateToggleButton(false);
    }

    gboolean handleSerialRead() {
        if (serialFd < 0) {
            return G_SOURCE_REMOVE;
        }

        char buffer[1024];
        std::string received;
        bool disconnected = false;
        std::string readError;

        while (true) {
            ssize_t n = ::read(serialFd, buffer, sizeof(buffer));
            if (n > 0) {
                received.append(buffer, static_cast<size_t>(n));
            } else if (n == 0) {
                struct pollfd pfd {};
                pfd.fd = serialFd;
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
                readError = std::strerror(errno);
                disconnected = true;
                break;
            }
        }

        if (!received.empty()) {
            // Ensure valid UTF-8 for GtkTextBuffer
            gchar* validText = g_utf8_make_valid(received.data(), static_cast<gssize>(received.size()));
            GtkTextIter endIter;
            gtk_text_buffer_get_end_iter(receiveBuffer, &endIter);
            gtk_text_buffer_insert(receiveBuffer, &endIter, validText, -1);
            g_free(validText);

            gtk_text_buffer_get_end_iter(receiveBuffer, &endIter);
            GtkTextMark* endMark = gtk_text_buffer_get_insert(receiveBuffer);
            gtk_text_buffer_place_cursor(receiveBuffer, &endIter);
            gtk_text_view_scroll_to_mark(receiveView, endMark, 0.0, FALSE, 0.0, 1.0);
        }

        if (disconnected) {
            stopSerial();
            if (!readError.empty()) {
                showWarning(
                    trText("Serial Read Error", "Błąd odczytu z portu szeregowego"),
                    std::string(trText("Read error: ", "Błąd odczytu: ")) + readError);
            } else {
                showWarning(
                    trText("Serial Disconnected", "Rozłączono port szeregowy"),
                    trText("The serial port was closed or disconnected.",
                           "Port szeregowy został zamknięty lub odłączony."));
            }
            return G_SOURCE_REMOVE;
        }

        return G_SOURCE_CONTINUE;
    }

    void handleSend() {
        const char* rawText = gtk_editable_get_text(GTK_EDITABLE(textInput));
        std::string text = rawText ? rawText : "";

        // Trim whitespace to check for /clear
        std::string trimmed = text;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
        const auto last = trimmed.find_last_not_of(" \t\r\n");
        if (last != std::string::npos) {
            trimmed.erase(last + 1);
        } else {
            trimmed.clear();
        }

        if (trimmed == "/clear") {
            gtk_text_buffer_set_text(receiveBuffer, "", 0);
            gtk_editable_set_text(GTK_EDITABLE(textInput), "");
            return;
        }

        if (serialFd < 0) {
            showWarning(
                trText("Serial Port Stopped", "Port szeregowy zatrzymany"),
                trText("Start the serial port before sending data.",
                       "Uruchom port szeregowy przed wysłaniem danych."));
            return;
        }

        if (text.empty()) {
            return;
        }

        std::string error;
        if (!writeAllSerial(serialFd, text, &error)) {
            showWarning(
                trText("Serial Send Failed", "Nie udało się wysłać danych"),
                error);
        } else {
            gtk_editable_set_text(GTK_EDITABLE(textInput), "");
        }
    }

    void showCustomPortDialog() {
        AdwDialog* dialog = adw_alert_dialog_new(
            trText("Custom Serial Port", "Własny port szeregowy"),
            trText("Enter serial port path (e.g. /dev/ttyUSB0 or /dev/pts/2):",
                   "Podaj ścieżkę portu szeregowego (np. /dev/ttyUSB0 lub /dev/pts/2):"));

        GtkWidget* entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(entry), selectedPort.c_str());
        gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "/dev/ttyUSB0");
        adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);

        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "cancel", trText("Cancel", "Anuluj"));
        adw_alert_dialog_add_response(ADW_ALERT_DIALOG(dialog), "ok", trText("Select", "Wybierz"));
        adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog), "ok", ADW_RESPONSE_SUGGESTED);
        adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "ok");
        adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

        g_object_set_data(G_OBJECT(dialog), "port-entry", entry);

        g_signal_connect(
            dialog,
            "response",
            G_CALLBACK(+[](AdwAlertDialog* dlg, const char* response, gpointer userData) {
                auto* self = static_cast<AppState*>(userData);
                if (std::strcmp(response, "ok") == 0) {
                    auto* portEntry = GTK_EDITABLE(g_object_get_data(G_OBJECT(dlg), "port-entry"));
                    const char* txt = gtk_editable_get_text(portEntry);
                    if (txt) {
                        std::string path(txt);
                        path.erase(0, path.find_first_not_of(" \t\r\n"));
                        const auto end = path.find_last_not_of(" \t\r\n");
                        if (end != std::string::npos) {
                            path.erase(end + 1);
                        } else {
                            path.clear();
                        }
                        if (!path.empty()) {
                            self->selectPort(path);
                        }
                    }
                }
            }),
            this);

        adw_dialog_present(dialog, GTK_WIDGET(window));
    }

    void saveOutput() {
        GtkFileDialog* dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, trText("Save Output", "Zapisz wyjście"));
        gtk_file_dialog_set_initial_name(dialog, "serial-output.txt");

        GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

        GtkFileFilter* txtFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(txtFilter, trText("Text Files (*.txt)", "Pliki tekstowe (*.txt)"));
        gtk_file_filter_add_pattern(txtFilter, "*.txt");
        g_list_store_append(filters, txtFilter);
        g_object_unref(txtFilter);

        GtkFileFilter* logFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(logFilter, trText("Log Files (*.log)", "Pliki dziennika (*.log)"));
        gtk_file_filter_add_pattern(logFilter, "*.log");
        g_list_store_append(filters, logFilter);
        g_object_unref(logFilter);

        GtkFileFilter* allFilter = gtk_file_filter_new();
        gtk_file_filter_set_name(allFilter, trText("All Files (*)", "Wszystkie pliki (*)"));
        gtk_file_filter_add_pattern(allFilter, "*");
        g_list_store_append(filters, allFilter);
        g_object_unref(allFilter);

        gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
        g_object_unref(filters);

        gtk_file_dialog_save(
            dialog,
            GTK_WINDOW(window),
            nullptr,
            [](GObject* source, GAsyncResult* result, gpointer userData) {
                auto* self = static_cast<AppState*>(userData);
                GError* err = nullptr;
                GFile* file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &err);
                if (!file) {
                    if (err) {
                        g_error_free(err);
                    }
                    return;
                }

                GtkTextIter startIter, endIter;
                gtk_text_buffer_get_bounds(self->receiveBuffer, &startIter, &endIter);
                gchar* contents = gtk_text_buffer_get_text(self->receiveBuffer, &startIter, &endIter, FALSE);

                GError* writeErr = nullptr;
                if (!g_file_replace_contents(
                        file,
                        contents,
                        std::strlen(contents),
                        nullptr,
                        FALSE,
                        G_FILE_CREATE_REPLACE_DESTINATION,
                        nullptr,
                        nullptr,
                        &writeErr)) {
                    gchar* path = g_file_get_path(file);
                    std::string msg = std::string(trText("Could not save to ", "Nie udało się zapisać do ")) +
                                      (path ? path : "") + ": " +
                                      (writeErr ? writeErr->message : "");
                    g_free(path);
                    if (writeErr) {
                        g_error_free(writeErr);
                    }
                    self->showWarning(
                        trText("Save Output Failed", "Nie udało się zapisać wyjścia"),
                        msg);
                }

                g_free(contents);
                g_object_unref(file);
            },
            this);

        g_object_unref(dialog);
    }

    void showAboutDialog() {
        AdwDialog* about = adw_about_dialog_new();
        adw_about_dialog_set_application_name(
            ADW_ABOUT_DIALOG(about),
            trText("Serial Terminal", "Terminal szeregowy"));
        adw_about_dialog_set_version(ADW_ABOUT_DIALOG(about), APP_VERSION);
        adw_about_dialog_set_license_type(ADW_ABOUT_DIALOG(about), GTK_LICENSE_GPL_3_0);
        adw_about_dialog_set_comments(
            ADW_ABOUT_DIALOG(about),
            trText("A program for sending and receiving serial data.",
                   "Program do wysyłania i odbierania danych przez port szeregowy."));
        adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(about), "io.github.Szaru3.SerialTerminal");
        adw_dialog_present(about, GTK_WIDGET(window));
    }
};

void onActivate(GtkApplication* app, gpointer userData) {
    auto* state = static_cast<AppState*>(userData);
    state->app = ADW_APPLICATION(app);

    if (state->window) {
        gtk_window_present(GTK_WINDOW(state->window));
        return;
    }

    // Minimal custom CSS only for the green Start button and monospace terminal look
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        css,
        ".start-action {"
        "  background-color: #2e7d32;"
        "  color: white;"
        "  font-weight: bold;"
        "}"
        ".start-action:hover { background-color: #388e3c; }"
        ".start-action:active { background-color: #1b5e20; }"
        ".terminal-card {"
        "  border-radius: 12px;"
        "}"
        ".terminal-view {"
        "  font-family: monospace;"
        "  padding: 10px;"
        "  background-color: transparent;"
        "}"
        ".terminal-view text {"
        "  background-color: transparent;"
        "}");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    state->window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(state->window), trText("Serial Terminal", "Terminal szeregowy"));
    gtk_window_set_default_size(GTK_WINDOW(state->window), 800, 600);

    // Restore saved settings, or pick initial serial port if available
    state->loadSettings();
    if (state->selectedPort.empty()) {
        const auto initialPorts = detectSerialPorts();
        if (!initialPorts.empty()) {
            state->selectedPort = initialPorts.front();
        }
    }

    // Window actions
    GSimpleAction* disabledAction = g_simple_action_new("disabled-placeholder", nullptr);
    g_simple_action_set_enabled(disabledAction, FALSE);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(disabledAction));
    g_object_unref(disabledAction);

    state->portAction = g_simple_action_new_stateful(
        "select-port",
        G_VARIANT_TYPE_STRING,
        g_variant_new_string(state->selectedPort.c_str()));
    g_signal_connect(
        state->portAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction* /*act*/, GVariant* param, gpointer data) {
            auto* st = static_cast<AppState*>(data);
            st->selectPort(g_variant_get_string(param, nullptr));
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(state->portAction));

    state->baudAction = g_simple_action_new_stateful(
        "select-baud",
        G_VARIANT_TYPE_INT32,
        g_variant_new_int32(state->selectedBaudIndex));
    g_signal_connect(
        state->baudAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction* act, GVariant* param, gpointer data) {
            auto* st = static_cast<AppState*>(data);
            int idx = g_variant_get_int32(param);
            if (idx < 0 || idx >= static_cast<int>(std::size(kBaudOptions))) {
                return;
            }
            st->selectedBaudIndex = idx;
            st->saveSettings();
            g_simple_action_set_state(act, g_variant_new_int32(idx));
            st->updateInfoLabel();
            if (st->serialFd >= 0) {
                std::string err;
                if (!configureSerialPort(st->serialFd, kBaudOptions[idx].speed, &err)) {
                    st->showWarning(
                        trText("Baud Rate Change Failed", "Nie udało się zmienić prędkości transmisji"),
                        err);
                }
            }
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(state->baudAction));

    GSimpleAction* customPortAction = g_simple_action_new("custom-port", nullptr);
    g_signal_connect(
        customPortAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
            static_cast<AppState*>(data)->showCustomPortDialog();
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(customPortAction));
    g_object_unref(customPortAction);

    GSimpleAction* saveAction = g_simple_action_new("save", nullptr);
    g_signal_connect(
        saveAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
            static_cast<AppState*>(data)->saveOutput();
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(saveAction));
    g_object_unref(saveAction);

    GSimpleAction* aboutAction = g_simple_action_new("about", nullptr);
    g_signal_connect(
        aboutAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
            static_cast<AppState*>(data)->showAboutDialog();
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(aboutAction));
    g_object_unref(aboutAction);

    GSimpleAction* quitAction = g_simple_action_new("quit", nullptr);
    g_signal_connect(
        quitAction,
        "activate",
        G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
            auto* st = static_cast<AppState*>(data);
            st->stopSerial();
            gtk_window_close(GTK_WINDOW(st->window));
        }),
        state);
    g_action_map_add_action(G_ACTION_MAP(state->window), G_ACTION(quitAction));
    g_object_unref(quitAction);

    const char* saveAccels[] = {"<Primary>s", nullptr};
    gtk_application_set_accels_for_action(app, "win.save", saveAccels);
    const char* quitAccels[] = {"<Primary>q", nullptr};
    gtk_application_set_accels_for_action(app, "win.quit", quitAccels);

    // Header bar
    AdwHeaderBar* headerBar = ADW_HEADER_BAR(adw_header_bar_new());
    state->windowTitle = ADW_WINDOW_TITLE(
        adw_window_title_new(trText("Serial Terminal", "Terminal szeregowy"), ""));
    adw_header_bar_set_title_widget(headerBar, GTK_WIDGET(state->windowTitle));

    // Port selector button on the left
    state->portMenuButton = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->portMenuButton),
                                trText("Select Serial Port", "Wybierz port szeregowy"));
    state->rebuildPortMenu();
    // Refresh detected ports whenever the popover is opened
    gtk_menu_button_set_create_popup_func(
        state->portMenuButton,
        [](GtkMenuButton* /*btn*/, gpointer data) {
            static_cast<AppState*>(data)->rebuildPortMenu();
        },
        state,
        nullptr);
    adw_header_bar_pack_start(headerBar, GTK_WIDGET(state->portMenuButton));

    // Baud rate selector button on the left
    state->baudMenuButton = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_widget_set_tooltip_text(GTK_WIDGET(state->baudMenuButton),
                                trText("Select Baud Rate", "Wybierz prędkość transmisji"));
    GMenu* baudMenu = g_menu_new();
    for (int i = 0; i < static_cast<int>(std::size(kBaudOptions)); ++i) {
        std::string label = std::to_string(kBaudOptions[i].rate);
        GMenuItem* item = g_menu_item_new(label.c_str(), nullptr);
        g_menu_item_set_action_and_target_value(item, "win.select-baud", g_variant_new_int32(i));
        g_menu_append_item(baudMenu, item);
        g_object_unref(item);
    }
    gtk_menu_button_set_menu_model(state->baudMenuButton, G_MENU_MODEL(baudMenu));
    g_object_unref(baudMenu);
    adw_header_bar_pack_start(headerBar, GTK_WIDGET(state->baudMenuButton));

    // Primary menu button on the right
    GtkMenuButton* primaryMenuBtn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(primaryMenuBtn, "open-menu-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(primaryMenuBtn), trText("Main Menu", "Menu główne"));
    GMenu* primaryMenu = g_menu_new();
    g_menu_append(primaryMenu, trText("Save Output...", "Zapisz wyjście..."), "win.save");
    GMenu* appSection = g_menu_new();
    g_menu_append(appSection, trText("About Serial Terminal", "O programie"), "win.about");
    g_menu_append(appSection, trText("Quit", "Zakończ"), "win.quit");
    g_menu_append_section(primaryMenu, nullptr, G_MENU_MODEL(appSection));
    g_object_unref(appSection);
    gtk_menu_button_set_menu_model(primaryMenuBtn, G_MENU_MODEL(primaryMenu));
    g_object_unref(primaryMenu);
    adw_header_bar_pack_end(headerBar, GTK_WIDGET(primaryMenuBtn));

    // Main content layout
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(contentBox, 12);
    gtk_widget_set_margin_bottom(contentBox, 16);
    gtk_widget_set_margin_start(contentBox, 16);
    gtk_widget_set_margin_end(contentBox, 16);

    // Receive text area inside a rounded card/scrolled window
    GtkWidget* scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_overflow(scrolled, GTK_OVERFLOW_HIDDEN);
    gtk_widget_add_css_class(scrolled, "card");
    gtk_widget_add_css_class(scrolled, "terminal-card");

    state->receiveView = GTK_TEXT_VIEW(gtk_text_view_new());
    state->receiveBuffer = gtk_text_view_get_buffer(state->receiveView);
    gtk_text_view_set_editable(state->receiveView, FALSE);
    gtk_text_view_set_cursor_visible(state->receiveView, FALSE);
    gtk_text_view_set_wrap_mode(state->receiveView, GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(GTK_WIDGET(state->receiveView), "terminal-view");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(state->receiveView));

    // Input row: Entry + Send + Start/Stop
    GtkWidget* inputRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    state->textInput = GTK_ENTRY(gtk_entry_new());
    gtk_widget_set_hexpand(GTK_WIDGET(state->textInput), TRUE);
    gtk_entry_set_placeholder_text(
        state->textInput,
        trText("Enter text to send...", "Wpisz tekst do wysłania..."));
    g_signal_connect(
        state->textInput,
        "activate",
        G_CALLBACK(+[](GtkEntry*, gpointer data) {
            static_cast<AppState*>(data)->handleSend();
        }),
        state);

    state->sendButton = GTK_BUTTON(gtk_button_new_with_label(trText("Send", "Wyślij")));
    gtk_widget_add_css_class(GTK_WIDGET(state->sendButton), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(state->sendButton), FALSE);
    g_signal_connect(
        state->sendButton,
        "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer data) {
            static_cast<AppState*>(data)->handleSend();
        }),
        state);

    state->toggleButton = GTK_BUTTON(gtk_button_new_with_label(trText("Start", "Uruchom")));
    gtk_widget_set_size_request(GTK_WIDGET(state->toggleButton), 104, -1);
    state->updateToggleButton(false);
    g_signal_connect(
        state->toggleButton,
        "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto* st = static_cast<AppState*>(data);
            if (st->serialFd >= 0) {
                st->stopSerial();
            } else {
                st->startSerial();
            }
        }),
        state);

    gtk_box_append(GTK_BOX(inputRow), GTK_WIDGET(state->textInput));
    gtk_box_append(GTK_BOX(inputRow), GTK_WIDGET(state->sendButton));
    gtk_box_append(GTK_BOX(inputRow), GTK_WIDGET(state->toggleButton));

    gtk_box_append(GTK_BOX(contentBox), scrolled);
    gtk_box_append(GTK_BOX(contentBox), inputRow);

    state->updateInfoLabel();

    // Assemble AdwToolbarView
    AdwToolbarView* toolbarView = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    adw_toolbar_view_add_top_bar(toolbarView, GTK_WIDGET(headerBar));
    adw_toolbar_view_set_content(toolbarView, contentBox);
    adw_application_window_set_content(state->window, GTK_WIDGET(toolbarView));

    g_signal_connect(
        state->window,
        "destroy",
        G_CALLBACK(+[](GtkWidget*, gpointer data) {
            auto* st = static_cast<AppState*>(data);
            st->sendButton = nullptr;
            st->toggleButton = nullptr;
            st->windowTitle = nullptr;
            st->portMenuButton = nullptr;
            st->baudMenuButton = nullptr;
            st->receiveView = nullptr;
            st->receiveBuffer = nullptr;
            st->textInput = nullptr;
            st->window = nullptr;
            st->stopSerial();
        }),
        state);

    gtk_window_present(GTK_WINDOW(state->window));
}

} // namespace

int main(int argc, char* argv[]) {
    AdwApplication* app = adw_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    AppState state;

    g_signal_connect(app, "activate", G_CALLBACK(onActivate), &state);
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    state.stopSerial();
    g_object_unref(app);
    return status;
}
