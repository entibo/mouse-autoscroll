/*
 * Generated with GPT 4.1
 *
 * mouse_accel.h - Standalone Mouse Pointer Acceleration Library
 *
 * Copyright © 2024 entibo
 * Based on code from libinput/filter-mouse.c
 *
 */

/*
    USAGE:

    mouse_accel_t accel;
    mouse_accel_init(&accel, dpi);

    double dx, dy; // unaccelerated deltas
    uint64_t timestamp_us; // timestamp in microseconds

    mouse_accel_feed(&accel, dx, dy, timestamp_us);

    double out_dx, out_dy;
    mouse_accel_get_accelerated(&accel, dx, dy, timestamp_us, &out_dx, &out_dy);

    // If changing speed:
    mouse_accel_set_speed(&accel, speed_adjustment); // [-1.0, +1.0]

    // When done:
    mouse_accel_destroy(&accel);
*/

#ifndef MOUSE_ACCEL_H
#define MOUSE_ACCEL_H

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* ====== Types ====== */

    typedef struct
    {
        double x, y;
    } mouse_accel_coords_t;

    typedef struct
    {
        uint64_t time;
        mouse_accel_coords_t delta;
    } mouse_accel_tracker_t;

// #define MOUSE_ACCEL_TRACKERS_MAX 64
#define MOUSE_ACCEL_TRACKERS_MAX 16

    typedef struct
    {
        mouse_accel_tracker_t points[MOUSE_ACCEL_TRACKERS_MAX];
        int npoints;
        int next;
    } mouse_accel_trackers_t;

    typedef struct
    {
        /* config */
        double threshold;
        double accel;
        double incline;
        int dpi;

        /* state */
        double last_velocity;
        mouse_accel_trackers_t trackers;
        double speed_adjustment;
    } mouse_accel_t;

    /* ====== Utility functions ====== */

    static inline double v_ms2us(double ms) { return ms / 1000.0; }
    static inline double v_us2ms(double us) { return us * 1000.0; }
    static inline double double_min(double a, double b) { return a < b ? a : b; }
    static inline double double_max(double a, double b) { return a > b ? a : b; }

    /* ====== Trackers ====== */

    static void mouse_accel_trackers_init(mouse_accel_trackers_t *t, int points)
    {
        memset(t, 0, sizeof(*t));
        t->npoints = (points > MOUSE_ACCEL_TRACKERS_MAX) ? MOUSE_ACCEL_TRACKERS_MAX : points;
        t->next = 0;
    }

    static void mouse_accel_trackers_feed(mouse_accel_trackers_t *t, double dx, double dy, uint64_t time)
    {
        t->points[t->next].delta.x = dx;
        t->points[t->next].delta.y = dy;
        t->points[t->next].time = time;
        t->next = (t->next + 1) % t->npoints;
    }

    static void mouse_accel_trackers_reset(mouse_accel_trackers_t *t, uint64_t time)
    {
        for (int i = 0; i < t->npoints; ++i)
        {
            t->points[i].delta.x = 0;
            t->points[i].delta.y = 0;
            t->points[i].time = time;
        }
        t->next = 0;
    }

    /* Calculate velocity in units/us */
    static double mouse_accel_trackers_velocity(mouse_accel_trackers_t *t, uint64_t now)
    {
        double dx = 0, dy = 0;
        uint64_t oldest = now, newest = 0;
        int points = t->npoints;

        for (int i = 0; i < points; ++i)
        {
            dx += t->points[i].delta.x;
            dy += t->points[i].delta.y;
            if (t->points[i].time < oldest)
                oldest = t->points[i].time;
            if (t->points[i].time > newest)
                newest = t->points[i].time;
        }
        uint64_t dt = newest > oldest ? (newest - oldest) : 1;
        double dist = sqrt(dx * dx + dy * dy);

        // printf("dx,dy: %.2f,%.2f | dist: %.2f | dt_us: %lu | vel: %.5f\n",
        //        dx, dy, dist, dt, dist / (double)dt);

        return dist / (double)dt;
    }

    /* ====== Acceleration Profile ====== */

#define MOUSE_ACCEL_DEFAULT_THRESHOLD v_ms2us(0.4)
#define MOUSE_ACCEL_MINIMUM_THRESHOLD v_ms2us(0.2)
#define MOUSE_ACCEL_DEFAULT_ACCELERATION 2.0
#define MOUSE_ACCEL_DEFAULT_INCLINE 1.1

    static double mouse_accel_profile_linear(mouse_accel_t *accel, double speed_in)
    {
        double max_accel = accel->accel;
        double threshold = accel->threshold;
        const double incline = accel->incline;
        double dpi_factor = accel->dpi / (double)1000;
        double factor;

        max_accel /= dpi_factor;
        threshold *= dpi_factor;

        if (v_us2ms(speed_in) < 0.07)
        {
            factor = 10 * v_us2ms(speed_in) + 0.3;
        }
        else if (speed_in < threshold)
        {
            factor = 1;
        }
        else
        {
            factor = incline * v_us2ms(speed_in - threshold) + 1;
        }
        factor = double_min(max_accel, factor);
        return factor;
    }

    /* ====== Main API ====== */

    /* Initialize mouse_accel struct.
     * dpi: physical device DPI (dots per inch)
     */
    static void mouse_accel_init(mouse_accel_t *accel, int dpi)
    {
        memset(accel, 0, sizeof(*accel));
        accel->threshold = MOUSE_ACCEL_DEFAULT_THRESHOLD;
        accel->accel = MOUSE_ACCEL_DEFAULT_ACCELERATION;
        accel->incline = MOUSE_ACCEL_DEFAULT_INCLINE;
        accel->dpi = dpi;
        // printf("[threshold=%.2f,accel=%.2f,incline=%.2f,dpi=%d]\n",
        //        accel->threshold, accel->accel, accel->incline, accel->dpi);
        mouse_accel_trackers_init(&accel->trackers, 16);
        accel->last_velocity = 0.0;
        accel->speed_adjustment = 0.0;
    }

    static void mouse_accel_destroy(mouse_accel_t *accel)
    {
        /* nothing to do */
    }

    static void mouse_accel_set_speed(mouse_accel_t *accel, double speed_adjustment)
    {
        if (speed_adjustment < -1.0)
            speed_adjustment = -1.0;
        if (speed_adjustment > 1.0)
            speed_adjustment = 1.0;

        accel->threshold = MOUSE_ACCEL_DEFAULT_THRESHOLD - v_ms2us(0.25) * speed_adjustment;
        if (accel->threshold < MOUSE_ACCEL_MINIMUM_THRESHOLD)
            accel->threshold = MOUSE_ACCEL_MINIMUM_THRESHOLD;

        accel->accel = MOUSE_ACCEL_DEFAULT_ACCELERATION + speed_adjustment * 1.5;
        accel->incline = MOUSE_ACCEL_DEFAULT_INCLINE + speed_adjustment * 0.75;
        accel->speed_adjustment = speed_adjustment;
    }

    /* Feed a new unaccelerated delta sample to the filter */
    static void mouse_accel_feed(mouse_accel_t *accel, double dx, double dy, uint64_t time_us)
    {
        /* Normalize for DPI: units are in 1000dpi */
        double norm_dx = dx * (1000.0 / accel->dpi);
        double norm_dy = dy * (1000.0 / accel->dpi);
        mouse_accel_trackers_feed(&accel->trackers, norm_dx, norm_dy, time_us);
    }

    /* Get the accelerated delta for the given (unaccelerated) input.
     * dx, dy: device deltas (pixels)
     * timestamp_us: timestamp in microseconds
     * out_dx, out_dy: outputs (normalized units)
     */
    static void mouse_accel_get_accelerated(mouse_accel_t *accel, double dx, double dy, uint64_t timestamp_us,
                                            double *out_dx, double *out_dy)
    {
        /* Normalize for DPI: units are in 1000dpi */
        double norm_dx = dx * (1000.0 / accel->dpi);
        double norm_dy = dy * (1000.0 / accel->dpi);

        /* Update tracker */
        mouse_accel_trackers_feed(&accel->trackers, norm_dx, norm_dy, timestamp_us);

        /* Calculate velocity */
        double velocity = mouse_accel_trackers_velocity(&accel->trackers, timestamp_us);

        // printf("dx,dy: %.2f,%.2f | timestamp: %lu | velocity: %.2f\n",
        //        dx, dy, timestamp_us, velocity);

        /* Calculate acceleration factor */
        double accel_factor = mouse_accel_profile_linear(accel, velocity);

        /* Output accelerated deltas */
        *out_dx = norm_dx * accel_factor;
        *out_dy = norm_dy * accel_factor;

        accel->last_velocity = velocity;
    }

#ifdef __cplusplus
}
#endif

#endif /* MOUSE_ACCEL_H */