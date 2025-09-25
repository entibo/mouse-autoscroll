
#ifndef DBUS_H
#define DBUS_H
#include <dbus/dbus.h>
void connect_dbus();
void send_dbus_message(const char *method);
void touch_press();
void touch_release();
#endif