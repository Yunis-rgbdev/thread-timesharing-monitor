#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/*
 * ============================================================
 * CONFIGURATION
 * ============================================================
 */

#define NUM_THREADS        6

#define WINDOW_WIDTH       1400
#define WINDOW_HEIGHT      900

#define FPS                 60

/*
 * Educational time slice.
 *
 * This is NOT the Linux kernel's actual scheduler quantum.
 * We use it to make the scheduling behavior easy to see.
 */
#define TIME_SLICE_MS       100

/*
 * How often the timeline records an event.
 */
#define TIMELINE_SIZE       180


/*
 * ============================================================
 * COLORS
 * ============================================================
 */

typedef struct {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} Color;


static const Color COLOR_BACKGROUND = {
    16, 18, 24, 255
};

static const Color COLOR_PANEL = {
    25, 28, 36, 255
};

static const Color COLOR_PANEL_LIGHT = {
    32, 36, 46, 255
};

static const Color COLOR_BORDER = {
    55, 60, 72, 255
};

static const Color COLOR_TEXT = {
    235, 238, 245, 255
};

static const Color COLOR_TEXT_DIM = {
    145, 150, 165, 255
};

static const Color COLOR_GREEN = {
    70, 220, 130, 255
};

static const Color COLOR_YELLOW = {
    245, 190, 70, 255
};

static const Color COLOR_RED = {
    235, 85, 90, 255
};

static const Color COLOR_BLUE = {
    80, 150, 245, 255
};

static const Color COLOR_PURPLE = {
    170, 110, 240, 255
};


/*
 * ============================================================
 * THREAD STATE
 * ============================================================
 */

typedef enum {
    THREAD_STOPPED,
    THREAD_WAITING,
    THREAD_RUNNING
} ThreadState;


typedef struct {

    int id;

    pthread_t pthread;

    unsigned long long counter;

    /*
     * Number of times the thread has been selected.
     */
    unsigned long long switches;

    /*
     * Number of milliseconds spent doing work.
     */
    unsigned long long runtime_ms;

    ThreadState state;

} CounterThread;


/*
 * ============================================================
 * TIMELINE
 * ============================================================
 *
 * Each item represents which thread was using the CPU.
 */

typedef struct {

    int thread_id;

    uint64_t timestamp;

} TimelineEvent;


/*
 * ============================================================
 * GLOBAL STATE
 * ============================================================
 */

static CounterThread threads[NUM_THREADS];

static TimelineEvent timeline[TIMELINE_SIZE];

static int timeline_count = 0;

static unsigned long long total_context_switches = 0;

static int current_thread = -1;

static int previous_thread = -1;

static bool program_running = true;

static bool simulation_running = true;


/*
 * Protects all shared state.
 */
static pthread_mutex_t state_mutex =
    PTHREAD_MUTEX_INITIALIZER;


/*
 * ============================================================
 * TIME
 * ============================================================
 */

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(
        CLOCK_MONOTONIC,
        &ts
    );

    return
        (uint64_t)ts.tv_sec * 1000ULL +
        (uint64_t)ts.tv_nsec / 1000000ULL;
}


/*
 * ============================================================
 * CPU AFFINITY
 * ============================================================
 *
 * Pin worker threads to CPU 0.
 *
 * Why?
 *
 * If you have 8 CPU cores, Linux could execute several
 * threads simultaneously.
 *
 * By putting all our demo threads on CPU 0, we can make
 * the time-sharing concept much easier to visualize.
 */

static void pin_to_cpu_zero(void)
{
#ifdef __linux__

    cpu_set_t cpuset;

    CPU_ZERO(&cpuset);

    CPU_SET(0, &cpuset);

    pthread_setaffinity_np(
        pthread_self(),
        sizeof(cpuset),
        &cpuset
    );

#endif
}


/*
 * ============================================================
 * ADD TIMELINE EVENT
 * ============================================================
 */

static void add_timeline_event(int thread_id)
{
    if (timeline_count >= TIMELINE_SIZE) {

        memmove(
            &timeline[0],
            &timeline[1],
            sizeof(TimelineEvent) *
            (TIMELINE_SIZE - 1)
        );

        timeline_count =
            TIMELINE_SIZE - 1;
    }

    timeline[timeline_count].thread_id =
        thread_id;

    timeline[timeline_count].timestamp =
        now_ms();

    timeline_count++;
}


/*
 * ============================================================
 * SELECT THREAD
 * ============================================================
 */

static void select_thread(int id)
{
    pthread_mutex_lock(&state_mutex);

    previous_thread =
        current_thread;

    current_thread = id;

    if (previous_thread != id) {

        total_context_switches++;
    }

    for (int i = 0; i < NUM_THREADS; i++) {

        if (i == id) {

            threads[i].state =
                THREAD_RUNNING;

        } else if (
            threads[i].state !=
            THREAD_STOPPED
        ) {

            threads[i].state =
                THREAD_WAITING;
        }
    }

    threads[id].switches++;

    add_timeline_event(id);

    pthread_mutex_unlock(&state_mutex);
}


/*
 * ============================================================
 * COUNTER THREAD
 * ============================================================
 */

static void *counter_worker(void *argument)
{
    CounterThread *thread =
        (CounterThread *)argument;

    pin_to_cpu_zero();

    while (program_running) {

        /*
         * Pause support.
         */
        if (!simulation_running) {

            pthread_mutex_lock(
                &state_mutex
            );

            if (
                threads[thread->id].state
                != THREAD_STOPPED
            ) {
                threads[thread->id].state =
                    THREAD_WAITING;
            }

            pthread_mutex_unlock(
                &state_mutex
            );

            usleep(10000);

            continue;
        }


        /*
         * Tell the visualizer that this thread
         * is now executing.
         */
        select_thread(thread->id);


        uint64_t start =
            now_ms();


        /*
         * Simulated CPU time slice.
         */
        while (
            program_running &&
            simulation_running &&
            now_ms() - start < TIME_SLICE_MS
        ) {

            /*
             * Perform some actual work.
             */
            for (int i = 0; i < 1000; i++) {

                volatile unsigned long long x;

                x = thread->counter;

                x *= 3;

                x /= 3;

                x += 1;
            }


            /*
             * Update counter safely.
             */
            pthread_mutex_lock(
                &state_mutex
            );

            thread->counter++;

            pthread_mutex_unlock(
                &state_mutex
            );
        }


        uint64_t elapsed =
            now_ms() - start;


        pthread_mutex_lock(
            &state_mutex
        );

        thread->runtime_ms += elapsed;

        if (
            thread->state !=
            THREAD_STOPPED
        ) {

            thread->state =
                THREAD_WAITING;
        }

        pthread_mutex_unlock(
            &state_mutex
        );


        /*
         * Give another runnable thread an opportunity.
         */
        sched_yield();
    }


    pthread_mutex_lock(
        &state_mutex
    );

    thread->state =
        THREAD_STOPPED;

    pthread_mutex_unlock(
        &state_mutex
    );

    return NULL;
}


/*
 * ============================================================
 * RESET
 * ============================================================
 */

static void reset_statistics(void)
{
    pthread_mutex_lock(
        &state_mutex
    );

    for (int i = 0; i < NUM_THREADS; i++) {

        threads[i].counter = 0;

        threads[i].switches = 0;

        threads[i].runtime_ms = 0;

        if (program_running) {

            threads[i].state =
                THREAD_WAITING;
        }
    }

    timeline_count = 0;

    total_context_switches = 0;

    current_thread = -1;

    previous_thread = -1;

    pthread_mutex_unlock(
        &state_mutex
    );
}


/*
 * ============================================================
 * SDL DRAWING HELPERS
 * ============================================================
 */

static void set_color(
    SDL_Renderer *renderer,
    Color color
)
{
    SDL_SetRenderDrawColor(
        renderer,
        color.r,
        color.g,
        color.b,
        color.a
    );
}


static void fill_rect(
    SDL_Renderer *renderer,
    int x,
    int y,
    int w,
    int h,
    Color color
)
{
    SDL_Rect rect = {
        x,
        y,
        w,
        h
    };

    set_color(
        renderer,
        color
    );

    SDL_RenderFillRect(
        renderer,
        &rect
    );
}


static void draw_rect(
    SDL_Renderer *renderer,
    int x,
    int y,
    int w,
    int h,
    Color color
)
{
    SDL_Rect rect = {
        x,
        y,
        w,
        h
    };

    set_color(
        renderer,
        color
    );

    SDL_RenderDrawRect(
        renderer,
        &rect
    );
}


/*
 * ============================================================
 * TEXT
 * ============================================================
 */

static void draw_text(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const char *text,
    int x,
    int y,
    Color color
)
{
    SDL_Color sdl_color = {
        color.r,
        color.g,
        color.b,
        color.a
    };

    SDL_Surface *surface =
        TTF_RenderUTF8_Blended(
            font,
            text,
            sdl_color
        );

    if (!surface)
        return;

    SDL_Texture *texture =
        SDL_CreateTextureFromSurface(
            renderer,
            surface
        );

    if (!texture) {

        SDL_FreeSurface(surface);

        return;
    }

    SDL_Rect dst = {
        x,
        y,
        surface->w,
        surface->h
    };

    SDL_RenderCopy(
        renderer,
        texture,
        NULL,
        &dst
    );

    SDL_DestroyTexture(texture);

    SDL_FreeSurface(surface);
}


/*
 * Draw centered text.
 */

static void draw_text_centered(
    SDL_Renderer *renderer,
    TTF_Font *font,
    const char *text,
    int center_x,
    int y,
    Color color
)
{
    int width;
    int height;

    TTF_SizeUTF8(
        font,
        text,
        &width,
        &height
    );

    draw_text(
        renderer,
        font,
        text,
        center_x - width / 2,
        y,
        color
    );
}


/*
 * ============================================================
 * THREAD COLORS
 * ============================================================
 */

static Color thread_color(int id)
{
    switch (id) {

        case 0:
            return COLOR_BLUE;

        case 1:
            return COLOR_GREEN;

        case 2:
            return COLOR_PURPLE;

        case 3:
            return (Color){240, 140, 70, 255};

        case 4:
            return (Color){70, 200, 210, 255};

        case 5:
            return (Color){220, 100, 170, 255};

        default:
            return COLOR_BLUE;
    }
}


/*
 * ============================================================
 * STATE NAME
 * ============================================================
 */

static const char *state_name(
    ThreadState state
)
{
    switch (state) {

        case THREAD_RUNNING:
            return "RUNNING";

        case THREAD_WAITING:
            return "WAITING";

        case THREAD_STOPPED:
            return "STOPPED";
    }

    return "UNKNOWN";
}


/*
 * ============================================================
 * STATE COLOR
 * ============================================================
 */

static Color state_color(
    ThreadState state
)
{
    switch (state) {

        case THREAD_RUNNING:
            return COLOR_GREEN;

        case THREAD_WAITING:
            return COLOR_YELLOW;

        case THREAD_STOPPED:
            return COLOR_RED;
    }

    return COLOR_TEXT_DIM;
}


/*
 * ============================================================
 * CPU PANEL
 * ============================================================
 */

static void draw_cpu_panel(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font
)
{
    int x = 40;
    int y = 80;
    int w = 420;
    int h = 260;


    /*
     * Panel.
     */

    fill_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_PANEL
    );

    draw_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_BORDER
    );


    draw_text(
        renderer,
        font,
        "CPU CORE",
        x + 20,
        y + 18,
        COLOR_TEXT
    );


    /*
     * CPU chip.
     */

    int chip_x = x + 25;
    int chip_y = y + 65;
    int chip_w = 170;
    int chip_h = 140;


    Color chip_color =
        current_thread >= 0
            ? COLOR_GREEN
            : COLOR_BORDER;


    fill_rect(
        renderer,
        chip_x,
        chip_y,
        chip_w,
        chip_h,
        COLOR_PANEL_LIGHT
    );


    draw_rect(
        renderer,
        chip_x,
        chip_y,
        chip_w,
        chip_h,
        chip_color
    );


    draw_text_centered(
        renderer,
        font,
        "CPU",
        chip_x + chip_w / 2,
        chip_y + 25,
        COLOR_TEXT
    );


    if (current_thread >= 0) {

        char label[64];

        snprintf(
            label,
            sizeof(label),
            "THREAD %d",
            current_thread
        );

        draw_text_centered(
            renderer,
            small_font,
            label,
            chip_x + chip_w / 2,
            chip_y + 65,
            thread_color(current_thread)
        );

        draw_text_centered(
            renderer,
            small_font,
            "EXECUTING",
            chip_x + chip_w / 2,
            chip_y + 95,
            COLOR_GREEN
        );

    } else {

        draw_text_centered(
            renderer,
            small_font,
            "IDLE",
            chip_x + chip_w / 2,
            chip_y + 70,
            COLOR_TEXT_DIM
        );
    }


    /*
     * Current thread information.
     */

    int info_x = x + 225;

    draw_text(
        renderer,
        small_font,
        "Currently running",
        info_x,
        y + 70,
        COLOR_TEXT_DIM
    );


    char running_text[64];

    if (current_thread >= 0) {

        snprintf(
            running_text,
            sizeof(running_text),
            "Thread %d",
            current_thread
        );

    } else {

        snprintf(
            running_text,
            sizeof(running_text),
            "None"
        );
    }


    draw_text(
        renderer,
        font,
        running_text,
        info_x,
        y + 95,
        COLOR_TEXT
    );


    /*
     * Time slice progress.
     */

    draw_text(
        renderer,
        small_font,
        "Time slice",
        info_x,
        y + 140,
        COLOR_TEXT_DIM
    );


    static uint64_t slice_start = 0;

    static int slice_thread = -1;


    if (current_thread != slice_thread) {

        slice_thread =
            current_thread;

        slice_start =
            now_ms();
    }


    float progress = 0.0f;


    if (
        simulation_running &&
        current_thread >= 0
    ) {

        uint64_t elapsed =
            now_ms() - slice_start;

        progress =
            (float)elapsed /
            (float)TIME_SLICE_MS;

        if (progress > 1.0f)
            progress = 1.0f;
    }


    /*
     * Progress background.
     */

    fill_rect(
        renderer,
        info_x,
        y + 165,
        190,
        18,
        COLOR_PANEL_LIGHT
    );


    fill_rect(
        renderer,
        info_x,
        y + 165,
        (int)(190 * progress),
        18,
        COLOR_GREEN
    );


    char percentage[32];

    snprintf(
        percentage,
        sizeof(percentage),
        "%d%%",
        (int)(progress * 100.0f)
    );


    draw_text(
        renderer,
        small_font,
        percentage,
        info_x,
        y + 190,
        COLOR_TEXT
    );


    /*
     * Explanation.
     */

    draw_text(
        renderer,
        small_font,
        "Threads take turns using CPU 0.",
        x + 20,
        y + 225,
        COLOR_TEXT_DIM
    );
}


/*
 * ============================================================
 * THREAD CARD
 * ============================================================
 */

static void draw_thread_card(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font,
    int id,
    int x,
    int y
)
{
    const int w = 410;
    const int h = 165;


    CounterThread *thread =
        &threads[id];


    /*
     * Copy values while protected.
     */

    pthread_mutex_lock(
        &state_mutex
    );

    unsigned long long counter =
        thread->counter;

    unsigned long long switches =
        thread->switches;

    unsigned long long runtime =
        thread->runtime_ms;

    ThreadState state =
        thread->state;

    pthread_mutex_unlock(
        &state_mutex
    );


    /*
     * Card.
     */

    Color border =
        id == current_thread
            ? COLOR_GREEN
            : COLOR_BORDER;


    fill_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_PANEL
    );


    draw_rect(
        renderer,
        x,
        y,
        w,
        h,
        border
    );


    /*
     * Colored thread indicator.
     */

    fill_rect(
        renderer,
        x + 15,
        y + 18,
        8,
        125,
        thread_color(id)
    );


    /*
     * Header.
     */

    char title[64];

    snprintf(
        title,
        sizeof(title),
        "THREAD %d",
        id
    );


    draw_text(
        renderer,
        font,
        title,
        x + 35,
        y + 15,
        COLOR_TEXT
    );


    /*
     * Status.
     */

    char status[64];

    snprintf(
        status,
        sizeof(status),
        "● %s",
        state_name(state)
    );


    draw_text(
        renderer,
        small_font,
        status,
        x + 35,
        y + 48,
        state_color(state)
    );


    /*
     * Counter.
     */

    draw_text(
        renderer,
        small_font,
        "COUNTER",
        x + 35,
        y + 82,
        COLOR_TEXT_DIM
    );


    char counter_text[64];

    snprintf(
        counter_text,
        sizeof(counter_text),
        "%llu",
        counter
    );


    draw_text(
        renderer,
        font,
        counter_text,
        x + 120,
        y + 77,
        COLOR_TEXT
    );


    /*
     * Statistics.
     */

    char switch_text[64];

    snprintf(
        switch_text,
        sizeof(switch_text),
        "Switches: %llu",
        switches
    );


    draw_text(
        renderer,
        small_font,
        switch_text,
        x + 35,
        y + 115,
        COLOR_TEXT_DIM
    );


    char runtime_text[64];

    snprintf(
        runtime_text,
        sizeof(runtime_text),
        "CPU time: %llums",
        runtime
    );


    draw_text(
        renderer,
        small_font,
        runtime_text,
        x + 190,
        y + 115,
        COLOR_TEXT_DIM
    );


    /*
     * Activity bar.
     */

    fill_rect(
        renderer,
        x + 35,
        y + 140,
        340,
        8,
        COLOR_PANEL_LIGHT
    );


    /*
     * Animated activity.
     */

    int activity =
        counter % 341;


    fill_rect(
        renderer,
        x + 35,
        y + 140,
        activity,
        8,
        thread_color(id)
    );
}


/*
 * ============================================================
 * THREAD GRID
 * ============================================================
 */

static void draw_thread_grid(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font
)
{
    int start_x = 40;
    int start_y = 370;

    int gap_x = 20;
    int gap_y = 20;

    int card_w = 410;
    int card_h = 165;


    for (int i = 0; i < NUM_THREADS; i++) {

        int column =
            i % 3;

        int row =
            i / 3;


        int x =
            start_x +
            column * (card_w + gap_x);

        int y =
            start_y +
            row * (card_h + gap_y);


        draw_thread_card(
            renderer,
            font,
            small_font,
            i,
            x,
            y
        );
    }
}


/*
 * ============================================================
 * READY QUEUE
 * ============================================================
 */

static void draw_ready_queue(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font
)
{
    int x = 900;
    int y = 80;
    int w = 460;
    int h = 260;


    fill_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_PANEL
    );


    draw_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_BORDER
    );


    draw_text(
        renderer,
        font,
        "READY QUEUE",
        x + 20,
        y + 18,
        COLOR_TEXT
    );


    draw_text(
        renderer,
        small_font,
        "Threads waiting for CPU time",
        x + 20,
        y + 52,
        COLOR_TEXT_DIM
    );


    /*
     * Queue boxes.
     */

    int box_x = x + 20;
    int box_y = y + 90;

    int box_w = 60;
    int box_h = 60;

    for (int i = 0; i < NUM_THREADS; i++) {

        if (i == current_thread)
            continue;


        fill_rect(
            renderer,
            box_x,
            box_y,
            box_w,
            box_h,
            COLOR_PANEL_LIGHT
        );


        draw_rect(
            renderer,
            box_x,
            box_y,
            box_w,
            box_h,
            thread_color(i)
        );


        char label[16];

        snprintf(
            label,
            sizeof(label),
            "T%d",
            i
        );


        draw_text_centered(
            renderer,
            font,
            label,
            box_x + box_w / 2,
            box_y + 18,
            thread_color(i)
        );


        box_x += 70;


        /*
         * Wrap to next row.
         */

        if (
            box_x + box_w >
            x + w - 20
        ) {

            box_x =
                x + 20;

            box_y += 80;
        }
    }


    /*
     * Explanation.
     */

    draw_text(
        renderer,
        small_font,
        "Green = CPU owner",
        x + 20,
        y + 205,
        COLOR_GREEN
    );


    draw_text(
        renderer,
        small_font,
        "Yellow = waiting",
        x + 180,
        y + 205,
        COLOR_YELLOW
    );
}


/*
 * ============================================================
 * TIMELINE
 * ============================================================
 */

static void draw_timeline(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font
)
{
    int x = 40;
    int y = 755;

    int w = 1320;
    int h = 120;


    fill_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_PANEL
    );


    draw_rect(
        renderer,
        x,
        y,
        w,
        h,
        COLOR_BORDER
    );


    draw_text(
        renderer,
        font,
        "CPU TIMELINE",
        x + 20,
        y + 12,
        COLOR_TEXT
    );


    draw_text(
        renderer,
        small_font,
        "Who used the CPU over time",
        x + 180,
        y + 15,
        COLOR_TEXT_DIM
    );


    /*
     * Timeline graph.
     */

    int graph_x = x + 100;
    int graph_y = y + 45;

    int graph_w = w - 130;

    int row_h = 10;
    int row_gap = 2;


    /*
     * Labels.
     */

    for (int i = 0; i < NUM_THREADS; i++) {

        char label[16];

        snprintf(
            label,
            sizeof(label),
            "T%d",
            i
        );


        draw_text(
            renderer,
            small_font,
            label,
            x + 25,
            graph_y +
            i * (row_h + row_gap) -
            3,
            thread_color(i)
        );
    }


    /*
     * Copy timeline safely.
     */

    TimelineEvent local[TIMELINE_SIZE];

    int count;


    pthread_mutex_lock(
        &state_mutex
    );

    count = timeline_count;

    memcpy(
        local,
        timeline,
        sizeof(TimelineEvent) *
        count
    );

    pthread_mutex_unlock(
        &state_mutex
    );


    if (count <= 0)
        return;


    /*
     * Each event gets a horizontal block.
     */

    float event_width =
        (float)graph_w /
        (float)TIMELINE_SIZE;


    int offset =
        TIMELINE_SIZE -
        count;


    for (int i = 0; i < count; i++) {

        int timeline_position =
            offset + i;


        int thread_id =
            local[i].thread_id;


        int block_x =
            graph_x +
            (int)(
                timeline_position *
                event_width
            );


        int block_y =
            graph_y +
            thread_id *
            (row_h + row_gap);


        int block_w =
            (int)event_width + 1;


        fill_rect(
            renderer,
            block_x,
            block_y,
            block_w,
            row_h,
            thread_color(thread_id)
        );
    }


    /*
     * Time direction.
     */

    draw_text(
        renderer,
        small_font,
        "PAST",
        graph_x,
        y + 103,
        COLOR_TEXT_DIM
    );


    draw_text(
        renderer,
        small_font,
        "NOW",
        graph_x + graph_w - 35,
        y + 103,
        COLOR_GREEN
    );
}


/*
 * ============================================================
 * TOP BAR
 * ============================================================
 */

static void draw_top_bar(
    SDL_Renderer *renderer,
    TTF_Font *font,
    TTF_Font *small_font
)
{
    fill_rect(
        renderer,
        0,
        0,
        WINDOW_WIDTH,
        60,
        COLOR_PANEL
    );


    draw_text(
        renderer,
        font,
        "THREAD TIME-SHARING MONITOR",
        30,
        14,
        COLOR_TEXT
    );


    /*
     * Simulation status.
     */

    Color status_color =
        simulation_running
            ? COLOR_GREEN
            : COLOR_YELLOW;


    const char *status =
        simulation_running
            ? "● RUNNING"
            : "● PAUSED";


    draw_text(
        renderer,
        small_font,
        status,
        1080,
        20,
        status_color
    );
}


/*
 * ============================================================
 * BOTTOM STATISTICS
 * ============================================================
 */

static void draw_statistics(
    SDL_Renderer *renderer,
    TTF_Font *small_font
)
{
    char text[128];


    snprintf(
        text,
        sizeof(text),
        "Context switches: %llu",
        total_context_switches
    );


    draw_text(
        renderer,
        small_font,
        text,
        40,
        885,
        COLOR_TEXT_DIM
    );


    snprintf(
        text,
        sizeof(text),
        "Threads: %d",
        NUM_THREADS
    );


    draw_text(
        renderer,
        small_font,
        text,
        280,
        885,
        COLOR_TEXT_DIM
    );


    draw_text(
        renderer,
        small_font,
        "SPACE: Pause / Resume",
        470,
        885,
        COLOR_TEXT_DIM
    );


    draw_text(
        renderer,
        small_font,
        "R: Reset",
        700,
        885,
        COLOR_TEXT_DIM
    );


    draw_text(
        renderer,
        small_font,
        "ESC: Exit",
        820,
        885,
        COLOR_TEXT_DIM
    );
}


/*
 * ============================================================
 * MAIN
 * ============================================================
 */

int main(void)
{
    /*
     * --------------------------------------------------------
     * INITIALIZE THREAD DATA
     * --------------------------------------------------------
     */

    for (int i = 0; i < NUM_THREADS; i++) {

        threads[i].id = i;

        threads[i].counter = 0;

        threads[i].switches = 0;

        threads[i].runtime_ms = 0;

        threads[i].state =
            THREAD_WAITING;
    }


    /*
     * --------------------------------------------------------
     * SDL
     * --------------------------------------------------------
     */

    if (
        SDL_Init(SDL_INIT_VIDEO)
        != 0
    ) {

        fprintf(
            stderr,
            "SDL_Init failed: %s\n",
            SDL_GetError()
        );

        return 1;
    }


    if (
        TTF_Init()
        != 0
    ) {

        fprintf(
            stderr,
            "TTF_Init failed: %s\n",
            TTF_GetError()
        );

        SDL_Quit();

        return 1;
    }


    /*
     * --------------------------------------------------------
     * FONT
     * --------------------------------------------------------
     */

    const char *font_path =
        "/usr/share/fonts/TTF/DejaVuSans.ttf";


    TTF_Font *font =
        TTF_OpenFont(
            font_path,
            22
        );


    TTF_Font *small_font =
        TTF_OpenFont(
            font_path,
            16
        );


    if (!font || !small_font) {

        fprintf(
            stderr,
            "Could not load font: %s\n",
            TTF_GetError()
        );

        TTF_Quit();

        SDL_Quit();

        return 1;
    }


    /*
     * --------------------------------------------------------
     * WINDOW
     * --------------------------------------------------------
     */

    SDL_Window *window =
        SDL_CreateWindow(
            "Thread Time-Sharing Monitor",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            WINDOW_WIDTH,
            WINDOW_HEIGHT,
            SDL_WINDOW_SHOWN
        );


    if (!window) {

        fprintf(
            stderr,
            "SDL_CreateWindow failed: %s\n",
            SDL_GetError()
        );

        TTF_CloseFont(font);
        TTF_CloseFont(small_font);

        TTF_Quit();
        SDL_Quit();

        return 1;
    }


    /*
     * --------------------------------------------------------
     * RENDERER
     * --------------------------------------------------------
     */

    SDL_Renderer *renderer =
        SDL_CreateRenderer(
            window,
            -1,
            SDL_RENDERER_ACCELERATED |
            SDL_RENDERER_PRESENTVSYNC
        );


    if (!renderer) {

        fprintf(
            stderr,
            "SDL_CreateRenderer failed: %s\n",
            SDL_GetError()
        );

        SDL_DestroyWindow(window);

        TTF_CloseFont(font);
        TTF_CloseFont(small_font);

        TTF_Quit();
        SDL_Quit();

        return 1;
    }


    /*
     * --------------------------------------------------------
     * CREATE WORKER THREADS
     * --------------------------------------------------------
     */

    for (int i = 0; i < NUM_THREADS; i++) {

        int result =
            pthread_create(
                &threads[i].pthread,
                NULL,
                counter_worker,
                &threads[i]
            );


        if (result != 0) {

            fprintf(
                stderr,
                "Could not create thread %d\n",
                i
            );

            program_running = false;

            break;
        }
    }


    /*
     * --------------------------------------------------------
     * MAIN GUI LOOP
     * --------------------------------------------------------
     */

    bool quit = false;

    SDL_Event event;


    while (!quit) {

        /*
         * ----------------------------------------------
         * EVENTS
         * ----------------------------------------------
         */

        while (
            SDL_PollEvent(&event)
        ) {

            if (
                event.type ==
                SDL_QUIT
            ) {

                quit = true;
            }


            if (
                event.type ==
                SDL_KEYDOWN
            ) {

                SDL_Keycode key =
                    event.key.keysym.sym;


                /*
                 * ESC
                 */

                if (
                    key ==
                    SDLK_ESCAPE
                ) {

                    quit = true;
                }


                /*
                 * SPACE
                 */

                if (
                    key ==
                    SDLK_SPACE
                ) {

                    simulation_running =
                        !simulation_running;
                }


                /*
                 * R
                 */

                if (
                    key ==
                    SDLK_r
                ) {

                    reset_statistics();
                }
            }
        }


        /*
         * ----------------------------------------------
         * CLEAR SCREEN
         * ----------------------------------------------
         */

        set_color(
            renderer,
            COLOR_BACKGROUND
        );

        SDL_RenderClear(
            renderer
        );


        /*
         * ----------------------------------------------
         * DASHBOARD
         * ----------------------------------------------
         */

        draw_top_bar(
            renderer,
            font,
            small_font
        );


        draw_cpu_panel(
            renderer,
            font,
            small_font
        );


        draw_ready_queue(
            renderer,
            font,
            small_font
        );


        draw_thread_grid(
            renderer,
            font,
            small_font
        );


        draw_timeline(
            renderer,
            font,
            small_font
        );


        draw_statistics(
            renderer,
            small_font
        );


        /*
         * ----------------------------------------------
         * PRESENT
         * ----------------------------------------------
         */

        SDL_RenderPresent(
            renderer
        );


        SDL_Delay(
            1000 / FPS
        );
    }


    /*
     * --------------------------------------------------------
     * STOP WORKERS
     * --------------------------------------------------------
     */

    program_running = false;


    /*
     * --------------------------------------------------------
     * WAIT FOR THREADS
     * --------------------------------------------------------
     */

    for (int i = 0; i < NUM_THREADS; i++) {

        pthread_join(
            threads[i].pthread,
            NULL
        );
    }


    /*
     * --------------------------------------------------------
     * PRINT FINAL STATISTICS
     * --------------------------------------------------------
     */

    printf(
        "\n========================================\n"
    );

    printf(
        "       THREAD SIMULATION RESULTS\n"
    );

    printf(
        "========================================\n\n"
    );


    for (int i = 0; i < NUM_THREADS; i++) {

        printf(
            "Thread %d\n"
            "  Counter:          %llu\n"
            "  Context switches: %llu\n"
            "  CPU runtime:      %llu ms\n\n",

            i,

            threads[i].counter,

            threads[i].switches,

            threads[i].runtime_ms
        );
    }


    printf(
        "Total context switches: %llu\n",
        total_context_switches
    );


    /*
     * --------------------------------------------------------
     * CLEANUP
     * --------------------------------------------------------
     */

    pthread_mutex_destroy(
        &state_mutex
    );


    TTF_CloseFont(font);

    TTF_CloseFont(small_font);

    TTF_Quit();


    SDL_DestroyRenderer(
        renderer
    );

    SDL_DestroyWindow(
        window
    );

    SDL_Quit();


    return 0;
}