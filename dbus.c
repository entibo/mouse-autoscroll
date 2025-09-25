#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>

DBusError err;
DBusConnection *conn;
DBusMessage *msg;
DBusMessageIter args;
dbus_uint32_t serial = 0;

void connect_dbus()
{
    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err) || !conn)
    {
        fprintf(stderr, "DBus: Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
    }
}

void send_dbus_message(const char *method)
{
    msg = dbus_message_new_method_call(
        "org.gnome.Shell",
        "/com/github/entibo/clicktotouch",
        "com.github.entibo.clicktotouch",
        method);

    if (!msg)
    {
        fprintf(stderr, "Message Null\n");
        exit(1);
    }

    if (!dbus_connection_send(conn, msg, &serial))
    {
        fprintf(stderr, "Out Of Memory!\n");
        exit(1);
    }
    dbus_connection_flush(conn);

    printf("Sent message with serial %u\n", serial);

    // Free message
    dbus_message_unref(msg);
}

void touch_press()
{
    send_dbus_message("Press");
}
void touch_release()
{
    send_dbus_message("Release");
}