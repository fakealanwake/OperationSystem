#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* мельче этого размера поток не создаём: сам вызов дороже сортировки */
#define CUTOFF 8192

static sem_t slots;
static pthread_mutex_t live_mu = PTHREAD_MUTEX_INITIALIZER;
static int live_threads;
static int peak_threads;

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void swap_int(int *x, int *y)
{
    int t = *x;
    *x = *y;
    *y = t;
}

static void note_live(int delta)
{
    pthread_mutex_lock(&live_mu);
    live_threads += delta;
    if (live_threads > peak_threads)
        peak_threads = live_threads;
    pthread_mutex_unlock(&live_mu);
}

/* Хоара: опорный элемент — середина, чтобы уже отсортированный массив
   не разваливался в худший случай. Возвращает границу правой части. */
static int partition(int *a, int left, int right)
{
    int pivot = a[left + (right - left) / 2];
    int i = left;
    int j = right;

    while (i <= j) {
        while (a[i] < pivot)
            i++;
        while (a[j] > pivot)
            j--;
        if (i <= j) {
            swap_int(&a[i], &a[j]);
            i++;
            j--;
        }
    }
    return i;
}

static void insertion_sort(int *a, int left, int right)
{
    for (int i = left + 1; i <= right; i++) {
        int key = a[i];
        int j = i - 1;
        while (j >= left && a[j] > key) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

typedef struct {
    int *a;
    int left;
    int right;
    int parallel;
} Task;

static void sort_range(int *a, int left, int right, int parallel);

static void *worker(void *arg)
{
    Task *t = arg;
    sort_range(t->a, t->left, t->right, t->parallel);
    free(t);
    return NULL;
}

static void sort_range(int *a, int left, int right, int parallel)
{
    if (left >= right)
        return;
    if (right - left < 32) {
        insertion_sort(a, left, right);
        return;
    }

    int mid = partition(a, left, right);

    /* одну половину можно отдать потоку, только если кусок большой
       и есть свободный слот. trywait не ждёт: иначе все потоки
       встанут друг за другом и параллелизма не будет */
    if (parallel && right - left >= CUTOFF && sem_trywait(&slots) == 0) {
        Task *t = malloc(sizeof(*t));
        if (!t)
            die("malloc");
        t->a = a;
        t->left = left;
        t->right = mid - 1;
        t->parallel = 1;

        pthread_t th;
        if (pthread_create(&th, NULL, worker, t) == 0) {
            note_live(1);
            sort_range(a, mid, right, 1);
            pthread_join(th, NULL);
            note_live(-1);
            /* слот отдаём после join: до этого поток ещё существует */
            sem_post(&slots);
            return;
        }
        free(t);
        sem_post(&slots);
    }

    sort_range(a, left, mid - 1, parallel);
    sort_range(a, mid, right, parallel);
}

static int *copy_array(const int *src, int n)
{
    int *dst = malloc((size_t)n * sizeof(int));
    if (!dst)
        die("malloc");
    memcpy(dst, src, (size_t)n * sizeof(int));
    return dst;
}

static int is_sorted(const int *a, int n)
{
    for (int i = 1; i < n; i++) {
        if (a[i] < a[i - 1])
            return 0;
    }
    return 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "использование: %s -t потоки [-n количество] [-s зерно]\n"
            "без -n числа читаются из stdin: сначала n, потом n целых\n",
            argv0);
}

int main(int argc, char **argv)
{
    int threads = 0;
    int n = 0;
    int have_n = 0;
    unsigned seed = (unsigned)time(NULL);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = atoi(argv[++i]);
            have_n = 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seed = (unsigned)atoi(argv[++i]);
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (threads < 1) {
        usage(argv[0]);
        return 1;
    }

    int *src;
    if (have_n) {
        if (n < 1) {
            fprintf(stderr, "n должно быть > 0\n");
            return 1;
        }
        src = malloc((size_t)n * sizeof(int));
        if (!src)
            die("malloc");
        srand(seed);
        for (int i = 0; i < n; i++)
            src[i] = rand();
    } else {
        if (scanf("%d", &n) != 1 || n < 1) {
            fprintf(stderr, "ожидалось количество элементов\n");
            return 1;
        }
        src = malloc((size_t)n * sizeof(int));
        if (!src)
            die("malloc");
        for (int i = 0; i < n; i++) {
            if (scanf("%d", &src[i]) != 1) {
                fprintf(stderr, "не хватает чисел\n");
                free(src);
                return 1;
            }
        }
    }

    /* главный поток уже запущен, поэтому дополнительных слотов на один меньше.
       при -t 1 семафор пустой и сортировка идёт в одном потоке */
    if (sem_init(&slots, 0, (unsigned)(threads - 1)) != 0)
        die("sem_init");

    int *seq = copy_array(src, n);
    int *par = copy_array(src, n);

    /* первый проход выкидываем: он прогревает кэш, и вторая сортировка
       без этого выглядит быстрее просто из-за памяти, а не из-за потоков */
    int *warm = copy_array(src, n);
    sort_range(warm, 0, n - 1, 0);
    free(warm);

    double t_seq = now_ms();
    sort_range(seq, 0, n - 1, 0);
    t_seq = now_ms() - t_seq;

    live_threads = 1;
    peak_threads = 1;
    fprintf(stderr, "pid=%d\n", (int)getpid());
    fflush(stderr);

    double t_par = now_ms();
    sort_range(par, 0, n - 1, 1);
    t_par = now_ms() - t_par;

    if (!is_sorted(seq, n) || !is_sorted(par, n) || memcmp(seq, par, (size_t)n * sizeof(int)) != 0) {
        fprintf(stderr, "ошибка: результат не совпал\n");
        return 1;
    }

    double speedup = t_par > 0.0 ? t_seq / t_par : 0.0;
    double efficiency = speedup / threads;

    printf("n=%d threads=%d\n", n, threads);
    printf("seq_ms=%.3f par_ms=%.3f\n", t_seq, t_par);
    printf("speedup=%.3f efficiency=%.3f peak_threads=%d\n", speedup, efficiency, peak_threads);

    if (n <= 40) {
        for (int i = 0; i < n; i++)
            printf("%d%c", par[i], i + 1 == n ? '\n' : ' ');
    }

    sem_destroy(&slots);
    free(src);
    free(seq);
    free(par);
    return 0;
}
