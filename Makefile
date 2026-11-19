CXX = g++
QT_CFLAGS := $(shell pkg-config --cflags Qt6Widgets) -I/usr/include/KF6/KWindowSystem
QT_LIBS := $(shell pkg-config --libs Qt6Widgets) -lKF6WindowSystem
CXXFLAGS = -std=c++20 -Wall -Wextra -O2 -fPIC $(QT_CFLAGS)

TARGET = SerialTerminal

GTK_CFLAGS := $(shell pkg-config --cflags gtk4 libadwaita-1 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk4 libadwaita-1 2>/dev/null)
GNOME_TARGET = SerialTerminal-gnome

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET) $(QT_LIBS)

gnome: $(GNOME_TARGET)

$(GNOME_TARGET): main_gnome.cpp
	$(CXX) -std=c++20 -Wall -Wextra -O2 $(GTK_CFLAGS) main_gnome.cpp -o $(GNOME_TARGET) $(GTK_LIBS)

clean:
	rm -f $(TARGET) $(GNOME_TARGET) *.o *-protocol.c *-client-protocol.h

.PHONY: all gnome clean
