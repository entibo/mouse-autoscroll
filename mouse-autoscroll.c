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

#define DEADZONE 15

static inline uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000ull;
}

static inline int sign(int a)
{
    if (a == 0)
        return 0;
    return (a < 0) ? -1 : 1;
}

struct libevdev_uinput *mouse_uinput;

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

    libevdev_enable_event_code(evdev, EV_KEY, BTN_LEFT, NULL);
    libevdev_enable_event_code(evdev, EV_KEY, BTN_RIGHT, NULL);
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

void scroll_to_extreme(unsigned int ev_code, int sign)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1 * 10 * 1000;
    for (int i = 0; i < 100; i++)
    {
        libevdev_uinput_write_event(mouse_uinput, EV_REL, ev_code, -sign * 10000);
        libevdev_uinput_write_event(mouse_uinput, EV_SYN, SYN_REPORT, 0);
        nanosleep(&ts, NULL);
    }
}

int left_clicking = 0;
int right_clicking = 0;
int dx = 0, dy = 0;
int has_moved_x = 0, has_moved_y = 0;
int deceleration_x = 0, deceleration_y = 0;
uint64_t last_moved = 0;
void handle_event(
    struct libevdev_uinput *mouse_uinput,
    struct input_event *ev)
{
    if (ev->type == EV_KEY && ev->code == BTN_LEFT)
    {
        if (ev->value)
        {
            left_clicking = 1;
            if (right_clicking)
            {
                scroll_to_extreme(REL_WHEEL_HI_RES, sign(dy));
                dx = 0;
                dy = 0;
                return; // Skip left click
            }
        }
        else
        {
            left_clicking = 0;
        }
    }
    else if (ev->type == EV_KEY && ev->code == BTN_RIGHT)
    {
        if (ev->value)
        {
            right_clicking = 1;
            dx = 0;
            dy = 0;
            has_moved_x = 0;
            has_moved_y = 0;
            deceleration_x = 0;
            deceleration_y = 0;
            last_moved = now_us();
            return; // Skip the mouse button press event
        }
        else
        {
            right_clicking = 0;
            if (!has_moved_x && !has_moved_y)
            { // Unskip the mouse button press event
                libevdev_uinput_write_event(mouse_uinput, EV_KEY, BTN_RIGHT, 1);
            }
        }
    }
    if (ev->type == EV_REL && right_clicking)
    {
        if (ev->code == REL_X || ev->code == REL_Y)
        {
            last_moved = now_us();
        }

        if (ev->code == REL_X)
        {
            dx += ev->value;
            has_moved_x = 1;
        }
        else if (ev->code == REL_Y)
        {
            if (dy != 0 && sign(dy) != sign(ev->value))
            {
                if (deceleration_y == 0)
                {
                    deceleration_y = abs(dy) / DEADZONE;
                    printf("Set deceleration_y to %d\n", deceleration_y);
                }
            }
            else
            {
                deceleration_y = 0;
                printf("Set deceleration_y to %d\n", deceleration_y);
            }

            if (!has_moved_y)
            { // Skip deadzone when initiating movement
                dy = sign(ev->value) * DEADZONE + ev->value;
            }
            else if (deceleration_y)
            { // Decelerate in constant time
                dy = dy + ev->value * deceleration_y;
                if (sign(dy) == sign(ev->value))
                {
                    dy = 0;
                }
            }
            else
            {
                dy = dy + ev->value;
            }

            has_moved_y = 1;
        }

        printf("(%d,%d)\n", dx, dy);
    }

    // Passthrough
    libevdev_uinput_write_event(mouse_uinput, ev->type, ev->code, ev->value);
}

int factor(int v)
{
    v = v - DEADZONE;
    if (v <= 0)
        return 0;
    v = v / 2;
    return v;
    // if (v <= 3)
    //     return v;
    // return 3;
}

void run_timer_task(void)
{
    static uint64_t last = 0;
    uint64_t t = now_us();

    if (last == 0)
        last = t;

    uint64_t delta_us = t - last;
    // printf("Timer tick: Δt = %llu µs\n", (unsigned long long)delta_us);

    if (right_clicking && dy != 0)
    {
        int scroll_by = -sign(dy) * factor(abs(dy));
        libevdev_uinput_write_event(mouse_uinput, EV_REL, REL_WHEEL_HI_RES, scroll_by);
        libevdev_uinput_write_event(mouse_uinput, EV_SYN, SYN_REPORT, 0);

        if ((t - last_moved) > 200000)
        {
            if (abs(dy) < DEADZONE)
                dy = 0;
        }
    }

    last = t;
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

    // Open device file
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
    ts.it_interval.tv_nsec = 16666666;
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
                handle_event(mouse_uinput, &ev);
            }
        }

        // timer events
        if (fds[1].revents & POLLIN)
        {
            uint64_t expirations;
            read(tfd, &expirations, sizeof(expirations)); // must read to clear
            run_timer_task();
        }
    }
}
