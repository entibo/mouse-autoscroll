#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <errno.h>
#include "dbus.h"
#include "pointer_accel.h"

#define HANDLE_EVENT_REEMIT 0
#define HANDLE_EVENT_DROP 1

#define DEADZONE 15
#define TICK_INTERVAL_US 1000
#define DPI 1000

int btn_primary = BTN_LEFT;
int btn_secondary = BTN_RIGHT;

static inline uint64_t now_us(void)
{
    static struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000ull;
}
static inline void sleep_ms(int ms)
{
    static struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = ms * 1000 * 1000;
    nanosleep(&ts, NULL);
}

static inline int min(int a, int b)
{
    return a < b ? a : b;
}
static inline int max(int a, int b)
{
    return a > b ? a : b;
}
static inline int sign(int a)
{
    if (a == 0)
        return 0;
    return (a < 0) ? -1 : 1;
}

struct libevdev_uinput *mouse_uinput;
struct libevdev_uinput *keyboard_uinput;

struct libevdev_uinput *uinput_from_evdev(struct libevdev *evdev)
{
    struct libevdev_uinput *uinput = NULL;
    int create_uinput_errno = libevdev_uinput_create_from_device(
        evdev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput);
    if (create_uinput_errno < 0)
    {
        fprintf(stderr, "Failed creating virtual device: %s\n", strerror(-create_uinput_errno));
        exit(1);
    }
    return uinput;
}

struct libevdev_uinput *new_mouse_uinput()
{
    struct libevdev *evdev = libevdev_new();
    libevdev_set_name(evdev, "Simulated Mouse (Autoscroll)");

    libevdev_enable_property(evdev, INPUT_PROP_POINTER);

    libevdev_enable_event_type(evdev, EV_SYN);
    libevdev_enable_event_type(evdev, EV_MSC);
    libevdev_enable_event_code(evdev, EV_MSC, MSC_SCAN, NULL);

    libevdev_enable_event_code(evdev, EV_KEY, btn_primary, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, btn_secondary, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_MIDDLE, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_SIDE, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_EXTRA, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_FORWARD, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_BACK, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_TASK, NULL);

    libevdev_enable_event_type(evdev, EV_REL);
    libevdev_enable_event_code(evdev, EV_REL, REL_X, NULL);
    libevdev_enable_event_code(evdev, EV_REL, REL_Y, NULL);
    libevdev_enable_event_code(evdev, EV_REL, REL_WHEEL, NULL);
    libevdev_enable_event_code(evdev, EV_REL, REL_WHEEL_HI_RES, NULL);
    libevdev_enable_event_code(evdev, EV_REL, REL_HWHEEL, NULL);
    libevdev_enable_event_code(evdev, EV_REL, REL_HWHEEL_HI_RES, NULL);

    struct libevdev_uinput *uinput = uinput_from_evdev(evdev);
    libevdev_free(evdev);
    return uinput;
}

struct libevdev_uinput *new_keyboard_uinput()
{
    struct libevdev *evdev = libevdev_new();
    libevdev_set_name(evdev, "Simulated Keyboard (Autoscroll)");

    libevdev_enable_event_type(evdev, EV_SYN);
    libevdev_enable_event_type(evdev, EV_MSC);
    libevdev_enable_event_code(evdev, EV_MSC, MSC_SCAN, NULL);

    for (int key_code = 0; key_code <= 248; key_code++)
    {
        libevdev_enable_event_code(evdev, EV_KEY, key_code, NULL);
    }

    struct libevdev_uinput *uinput = uinput_from_evdev(evdev);
    libevdev_free(evdev);
    return uinput;
}

void scroll_multiple(int is_vertical, int value)
{
    printf("scroll_mutiple(%d, %d)\n", is_vertical, value);

    // libevdev_uinput_write_event(mouse_uinput, EV_REL, is_vertical ? REL_WHEEL : REL_HWHEEL, value);
    // libevdev_uinput_write_event(mouse_uinput, EV_REL, is_vertical ? REL_WHEEL_HI_RES : REL_HWHEEL_HI_RES, 120 * value);
    // libevdev_uinput_write_event(mouse_uinput, EV_SYN, SYN_REPORT, 0);

    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1 * 1 * 1000 * 1000;
    nanosleep(&ts, NULL);
    for (int i = 0; i < abs(value); i++)
    {
        libevdev_uinput_write_event(mouse_uinput, EV_REL, is_vertical ? REL_WHEEL : REL_HWHEEL, 1 * sign(value));
        libevdev_uinput_write_event(mouse_uinput, EV_REL, is_vertical ? REL_WHEEL_HI_RES : REL_HWHEEL_HI_RES, 120 * sign(value));
        libevdev_uinput_write_event(mouse_uinput, EV_SYN, SYN_REPORT, 0);
        printf("Scrolling by one unit of 120\n");
        nanosleep(&ts, NULL);
    }
}

//

#define STATE_WAITING_FOR_SECONDARY_PRESS 0
#define STATE_SCROLLING_WAITING 1
#define STATE_SCROLLING 2
#define STATE_SCROLLING_DISCRETE 3
#define STATE_ACTION_WAITING 4

mouse_accel_t accel;
int primary_pressed = 0;
int secondary_pressed = 0;
int state = STATE_WAITING_FOR_SECONDARY_PRESS;
int dx = 0, dy = 0;
int dir_x = 0, dir_y = 0;
double scroll_x = 0, scroll_y = 0;
double vel_x = 0, vel_y = 0;
uint64_t time_accumulator_us = 0;
uint64_t last_moved = 0;
uint64_t scroll_start_us = 0;

double delta_to_scroll_speed(double v)
{
    // v = v - DEADZONE;
    if (v <= 0)
        return 0;
    return 0.2;
    if (v < 50)
        return 0.7;
    return 0.7 + (v - 50) / 50;
    // if (v < 150)
    //     return 5;
    // return 10;
}

void tick()
{
    static uint64_t last = 0;
    uint64_t t = now_us();

    if (last == 0)
        last = t;

    uint64_t delta_us = t - last;
    // printf("Timer tick: Δt = %llu µs\n", (unsigned long long)delta_us);

    double f = (double)delta_us / 1000.0;

    double vel_update_rate = 0.05 * fmin(1, (double)(t - scroll_start_us) / (100 * 1000));

    double target_vel_y = delta_to_scroll_speed((double)abs(dy)) * sign(dy);
    vel_y = vel_y + (vel_update_rate * f) * ((double)(target_vel_y)-vel_y);
    scroll_y += vel_y * f;

    double target_vel_x = delta_to_scroll_speed((double)abs(dx)) * sign(dx);
    vel_x = vel_x + (vel_update_rate * f) * ((double)(target_vel_x)-vel_x);
    scroll_x += vel_x * f;

    // printf("vel_y: %.2f\n", vel_y);
    // printf("scroll_y: (before) %.2f (after) %.2f (+= %.2f) | scroll by %d\n",
    //        scroll_y, scroll_y - trunc(scroll_y), vel_y * f,
    //        (int)trunc(scroll_y));
    int do_syn = 0;
    if (abs(scroll_y) >= 1)
    {
        double scroll_value = trunc(scroll_y);
        scroll_y = scroll_y - scroll_value;
        libevdev_uinput_write_event(mouse_uinput, EV_REL, REL_WHEEL_HI_RES, -(int)scroll_value);
        do_syn = 1;
    }
    if (abs(scroll_x) >= 1)
    {
        double scroll_value = trunc(scroll_x);
        scroll_x = scroll_x - scroll_value;
        libevdev_uinput_write_event(mouse_uinput, EV_REL, REL_HWHEEL_HI_RES, (int)scroll_value);
        do_syn = 1;
    }
    if (do_syn)
        libevdev_uinput_write_event(mouse_uinput, EV_SYN, SYN_REPORT, 0);

    last = t;
}

int handle_primary_press()
{
    primary_pressed = 1;

    if (state == STATE_SCROLLING_WAITING)
    {
        libevdev_uinput_write_event(keyboard_uinput, EV_KEY, KEY_SPACE, 1);
        libevdev_uinput_write_event(keyboard_uinput, EV_KEY, KEY_SPACE, 0);
        libevdev_uinput_write_event(keyboard_uinput, EV_SYN, SYN_REPORT, 0);
        return HANDLE_EVENT_DROP;
    }
    if (state == STATE_SCROLLING)
    {
        // int is_vertical = abs(dir_y) >= abs(dir_x);
        // scroll_multiple(is_vertical, 3 * (is_vertical ? sign(dir_y) : sign(dir_x)));
        return HANDLE_EVENT_DROP;
    }

    return HANDLE_EVENT_REEMIT;
}
int handle_primary_release()
{
    primary_pressed = 0;
    return HANDLE_EVENT_REEMIT;
}

int handle_secondary_press()
{
    secondary_pressed = 1;
    dx = 0;
    dy = 0;
    dir_x = 0;
    dir_y = 0;
    last_moved = now_us();

    state = STATE_SCROLLING_WAITING;
    printf("state = STATE_SCROLLING_WAITING\n");
    return HANDLE_EVENT_DROP;
}
int handle_secondary_release()
{
    secondary_pressed = 0;

    if (state == STATE_SCROLLING)
    {
        dx = 0;
        dy = 0;
        state = STATE_WAITING_FOR_SECONDARY_PRESS;
        printf("state = STATE_WAITING_FOR_SECONDARY_PRESS\n");
        return HANDLE_EVENT_DROP;
    }

    if (state == STATE_SCROLLING_WAITING)
    { // Press secondary button and re-emit the release
        libevdev_uinput_write_event(mouse_uinput, EV_KEY, btn_secondary, 1);
        state = STATE_WAITING_FOR_SECONDARY_PRESS;
        printf("state = STATE_WAITING_FOR_SECONDARY_PRESS\n");
        return HANDLE_EVENT_REEMIT;
    }
    return HANDLE_EVENT_DROP;
}

int handle_move(int is_vertical, int value, uint64_t timestamp_us)
{
    last_moved = now_us();

    if (is_vertical)
    {
        dir_y += value;
        dir_y = sign(dir_y) * min(10, abs(dir_y));
        dir_x = sign(dir_x) * max(0, abs(dir_x) - abs(value));
    }
    else
    {
        dir_x += value;
        dir_x = sign(dir_x) * min(10, abs(dir_x));
        dir_y = sign(dir_y) * max(0, abs(dir_y) - abs(value));
    }

    mouse_accel_feed(&accel, is_vertical ? 0 : value, is_vertical ? value : 0, timestamp_us);
    double velocity = mouse_accel_trackers_velocity(&accel.trackers, timestamp_us);
    double accel_factor = mouse_accel_profile_linear(&accel, velocity);
    // printf("velocity_us: %.6f | velocity_ms: %.6f | Accel factor: %.6f\n",
    //        velocity, velocity * 1000, accel_factor);

    if (state == STATE_SCROLLING_WAITING || state == STATE_SCROLLING)
    {
        if (abs(dir_y) >= abs(dir_x))
        {
            if (sign(dy) != sign(dir_y))
            {
                dy = 0;
            }
            dy += value;
            scroll_y += value * accel_factor * 4;
            dx = 0;
        }
        else
        {
            if (sign(dx) != sign(dir_x))
            {
                dx = 0;
            }
            dx += value;
            scroll_x += value * accel_factor * 4;
            dy = 0;
        }
        // printf("dy=%d; dir_y=%d\n", dy, dir_y);
    }

    if (state == STATE_SCROLLING_WAITING)
    {
        state = STATE_SCROLLING;
        printf("state = STATE_SCROLLING\n");
        time_accumulator_us = 0;
        scroll_start_us = now_us();

        return HANDLE_EVENT_DROP;
    }

    if (state == STATE_SCROLLING)
    {
        return HANDLE_EVENT_DROP;
    }

    return HANDLE_EVENT_REEMIT;
}

int handle_scroll(int is_vertical, int value)
{
    printf("Scroll (%2d)\n", value);
    return HANDLE_EVENT_REEMIT;
}

//

void handle_mouse_event(struct input_event *ev)
{
    uint64_t timestamp_us = ev->time.tv_usec + 1000000 * ev->time.tv_sec;
    // printf("%ld\n", timestamp_us);

    int r = HANDLE_EVENT_REEMIT;
    if (ev->type == EV_KEY && ev->code == btn_primary)
    {
        if (ev->value)
            r = handle_primary_press();
        else
            r = handle_primary_release();
    }
    else if (ev->type == EV_KEY && ev->code == btn_secondary)
    {
        if (ev->value)
            r = handle_secondary_press();
        else
            r = handle_secondary_release();
    }
    else if (ev->type == EV_REL)
    {
        if (ev->code == REL_X || ev->code == REL_Y)
            r = handle_move(ev->code == REL_Y, ev->value, timestamp_us);
        else if (ev->code == REL_WHEEL_HI_RES || ev->code == REL_HWHEEL_HI_RES)
            r = handle_scroll(ev->code == REL_WHEEL_HI_RES, ev->value);
        else if (ev->code == REL_WHEEL || ev->code == REL_HWHEEL)
            r = HANDLE_EVENT_DROP;
    }
    else if (ev->type == MSC_SCAN)
    {
        r = HANDLE_EVENT_DROP;
    }
    if (r == HANDLE_EVENT_REEMIT)
    {
        libevdev_uinput_write_event(mouse_uinput, ev->type, ev->code, ev->value);
    }
}

int main(int argc, char *argv[])
{
    // Read CLI arguments
    if (argc < 2)
    {
        printf("Usage: %s <dev_path>\n", argv[0]);
        return 1;
    }
    char *dev_path = argv[1];

    // Open device file directly
    int dev_fd = open(dev_path, O_RDONLY | O_NONBLOCK);
    if (dev_fd < 0)
    {
        fprintf(stderr, "Open %s failed: %s\n", dev_path, strerror(-dev_fd));
        return 1;
    }
    struct libevdev *evdev = NULL;
    int rc = libevdev_new_from_fd(dev_fd, &evdev);
    if (rc < 0)
    {
        fprintf(stderr, "Open %s failed: %s\n", dev_path, strerror(-rc));
        return 1;
    }

    // Get exclusive rights to this device's events
    if (ioctl(dev_fd, EVIOCGRAB, (void *)1) < 0)
    {
        fprintf(stderr, "Grab %s failed\n", dev_path);
        return 1;
    }

    // Prepare virtual devices for output
    mouse_uinput = new_mouse_uinput();
    keyboard_uinput = new_keyboard_uinput();

    // Connect to GNOME extension using DBus
    // connect_dbus();

    // Init the acceleration thing
    mouse_accel_init(&accel, DPI);
    // mouse_accel_set_speed(&accel, 0.9); // ?

    // Event loop: read device events and run a callback at a regular interval

    struct input_event ev;

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (tfd == -1)
    {
        perror("timerfd_create");
        return 1;
    }

    struct itimerspec ts;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = TICK_INTERVAL_US * 1000;
    ts.it_value = ts.it_interval; // first expiry
    if (timerfd_settime(tfd, 0, &ts, NULL) == -1)
    {
        perror("timerfd_settime");
        return 1;
    }

    struct pollfd fds[2];
    fds[0].fd = dev_fd;
    fds[0].events = POLLIN;
    fds[1].fd = tfd;
    fds[1].events = POLLIN;

    while (1)
    {
        int rc = poll(fds, 2, -1);
        if (rc == -1)
        {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }

        // device events
        if (fds[0].revents & POLLIN)
        {
            while (libevdev_next_event(evdev,
                                       LIBEVDEV_READ_FLAG_NORMAL, &ev) ==
                   LIBEVDEV_READ_STATUS_SUCCESS)
            {
                handle_mouse_event(&ev);
            }
        }

        // timer events
        if (fds[1].revents & POLLIN)
        {
            uint64_t expirations;
            read(tfd, &expirations, sizeof(expirations)); // must read to clear
            tick();
        }
    }
}
