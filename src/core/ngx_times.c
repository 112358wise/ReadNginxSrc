
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * The time may be updated by signal handler or by several threads.
 * The time update operations are rare and require to hold the ngx_time_lock.
 * The time read operations are frequent, so they are lock-free and get time
 * values and strings from the current slot.  Thus thread may get the corrupted
 * values only if it is preempted while copying and then it is not scheduled
 * to run more than NGX_TIME_SLOTS seconds.
 */
//k 上面最后一句没看明白，时间读多，写少。如果出现冲突，肯定是中间抢占了并且NGX_TIME_SLOTS秒还没有开始运行
//k 我明白了，这里采用双缓冲的超级版本，64缓冲机制，时间的字符串值采用64秒一个轮回，
//k 通过下面的ngx_cached_*变量直接访问，而不是通过cached_err_log_time[slot]进行索引访问。这样冲突的概率非常小，64秒内还没有调度运行，才有可能。
//k 不过其实咱们能想到，取错误日志时间，正常时间，三者还是可能出现一秒的误差，不过这是在一秒的开始，结束，没啥关系的
#define NGX_TIME_SLOTS   64

static ngx_uint_t        slot;
static ngx_atomic_t      ngx_time_lock;//k 这锁高效啊，一个整数

volatile ngx_msec_t      ngx_current_msec;
volatile ngx_time_t     *ngx_cached_time;
volatile ngx_str_t       ngx_cached_err_log_time;
volatile ngx_str_t       ngx_cached_http_time;
volatile ngx_str_t       ngx_cached_http_log_time;

#if !(NGX_WIN32)

/*
 * locatime() and localtime_r() are not Async-Signal-Safe functions, therefore,
 * they must not be called by a signal handler, so we use the cached
 * GMT offset value. Fortunately the value is changed only two times a year.
 */

static ngx_int_t         cached_gmtoff;
#endif

static ngx_time_t        cached_time[NGX_TIME_SLOTS];
static u_char            cached_err_log_time[NGX_TIME_SLOTS]
                                    [sizeof("1970/09/28 12:00:00")];
static u_char            cached_http_time[NGX_TIME_SLOTS]
                                    [sizeof("Mon, 28 Sep 1970 06:00:00 GMT")];
static u_char            cached_http_log_time[NGX_TIME_SLOTS]
                                    [sizeof("28/Sep/1970:12:00:00 +0600")];


static char  *week[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static char  *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

void
ngx_time_init(void)
{
    ngx_cached_err_log_time.len = sizeof("1970/09/28 12:00:00") - 1;
    ngx_cached_http_time.len = sizeof("Mon, 28 Sep 1970 06:00:00 GMT") - 1;
    ngx_cached_http_log_time.len = sizeof("28/Sep/1970:12:00:00 +0600") - 1;

    ngx_cached_time = &cached_time[0];

    ngx_time_update();
}


/*k 
ngx_master_process_cycle会每次处理完信号后调用这里；
ngx_epoll_process_events如果设置了ngx_timer_resolution也会定期掉这里，这个是配置精确时间时用的
*/
void
ngx_time_update(void)
{
    u_char          *p0, *p1, *p2;
    ngx_tm_t         tm, gmt;//k struct tm 
    time_t           sec;
    ngx_uint_t       msec;
    ngx_time_t      *tp;
    struct timeval   tv;

    if (!ngx_trylock(&ngx_time_lock)) {
        return;//k 如果没拿到锁，那给力，有人在更新呢
    }

    ngx_gettimeofday(&tv);//k gettimeofday(tp, NULL);

    sec = tv.tv_sec;
    msec = tv.tv_usec / 1000;

    ngx_current_msec = (ngx_msec_t) sec * 1000 + msec;//k 当前的毫秒数，可以立即更新

    tp = &cached_time[slot];//k 引用全局的缓存时间slot位置

    if (tp->sec == sec) {
		//k 秒数相等，就只更新毫秒数了，其他不动，那其他时间值呢，因为下面的字符串值压根就不需要毫秒数，所以咱们可以直接退出啦
        tp->msec = msec;
        ngx_unlock(&ngx_time_lock);
        return;
    }

    if (slot == NGX_TIME_SLOTS - 1) {//k 不断轮询这个cached_time结构，到头了回0
        slot = 0;//k 这里就加1了，那其他线程读取这个值，怎么办?，我还没更新完呢，你们怎么也得取上一个吧
    } else {
        slot++;
    }

    tp = &cached_time[slot];//k 指向下一个槽位

    tp->sec = sec;//k 更新新的槽位的时间，让他等于最新的时间
    tp->msec = msec;

    ngx_gmtime(sec, &gmt);//k 搞一个字符串型的时间值用用
    p0 = &cached_http_time[slot][0];

    (void) ngx_sprintf(p0, "%s, %02d %s %4d %02d:%02d:%02d GMT",
                       week[gmt.ngx_tm_wday], gmt.ngx_tm_mday,
                       months[gmt.ngx_tm_mon - 1], gmt.ngx_tm_year,
                       gmt.ngx_tm_hour, gmt.ngx_tm_min, gmt.ngx_tm_sec);

#if (NGX_HAVE_GETTIMEZONE)

    tp->gmtoff = ngx_gettimezone();
    ngx_gmtime(sec + tp->gmtoff * 60, &tm);

#elif (NGX_HAVE_GMTOFF)

    ngx_localtime(sec, &tm);
    cached_gmtoff = (ngx_int_t) (tm.ngx_tm_gmtoff / 60);
    tp->gmtoff = cached_gmtoff;

#else

    ngx_localtime(sec, &tm);
    cached_gmtoff = ngx_timezone(tm.ngx_tm_isdst);
    tp->gmtoff = cached_gmtoff;

#endif

	//k 更新错误日志的时间值
    p1 = &cached_err_log_time[slot][0];

    (void) ngx_sprintf(p1, "%4d/%02d/%02d %02d:%02d:%02d",
                       tm.ngx_tm_year, tm.ngx_tm_mon,
                       tm.ngx_tm_mday, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec);


    p2 = &cached_http_log_time[slot][0];

    (void) ngx_sprintf(p2, "%02d/%s/%d:%02d:%02d:%02d %c%02d%02d",
                       tm.ngx_tm_mday, months[tm.ngx_tm_mon - 1],
                       tm.ngx_tm_year, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec,
                       tp->gmtoff < 0 ? '-' : '+',
                       ngx_abs(tp->gmtoff / 60), ngx_abs(tp->gmtoff % 60));


    ngx_memory_barrier();//k 这个作用是啥 � 
    //k 作用是，如果不加这个，编译器可能会将p1,p2,p3本来是123123的方式排列，优化为112233的方式，从而，应用程序读取的时间发生冲突的概率更大了。妙! 
//k 到这里才正式更新，更新后，其他地方就可以通过下面的结构直接访问到了，不用通过数组，slot来访问。相当于上面printf更新的时候，其他线程还是访问不到的
//k 这里相当于数组时用来做双缓冲的超级版本，64缓冲用的，64秒一个轮回，这样保证正常情况下，不会出现串了的情况
//k 不过还是有个小注意点，比如我先打一条ngx_cached_http_time日志，然后一条ngx_cached_http_log_time到文件中，然后发现，前面一条是10秒的时候，后面一条却是9秒，穿越了
//k 因为下面的四条的更新不是原子操作
    ngx_cached_time = tp;
    ngx_cached_http_time.data = p0;
    ngx_cached_err_log_time.data = p1;
    ngx_cached_http_log_time.data = p2;

    ngx_unlock(&ngx_time_lock);
}


#if !(NGX_WIN32)

void
ngx_time_sigsafe_update(void)
{//k 这个函数只更新cached_err_log_time时间,只在ngx_signal_handler里面用过,收到信号的时候
//k 取得最新时间，不是通过缓存的时间取的。但设置到了缓存的里面了，而且把slot增加了
    u_char          *p;
    ngx_tm_t         tm;
    time_t           sec;
    ngx_time_t      *tp;
    struct timeval   tv;

    if (!ngx_trylock(&ngx_time_lock)) {
        return;
    }

    ngx_gettimeofday(&tv);

    sec = tv.tv_sec;

    tp = &cached_time[slot];
	
    if (tp->sec == sec) {//k 如果秒数相等，不用更新啥的。这样一秒内多个信号，不会重复更新，凭白白费多个slot
        ngx_unlock(&ngx_time_lock);
        return;
    }

    if (slot == NGX_TIME_SLOTS - 1) {
        slot = 0;
    } else {//k 增加一个新的槽位，但是却不更新&cached_time[slot]处的时间值，而至更新cached_err_log_time时间，你明知道它有更新
    //k 这样我一秒给你来600个信号，第一个你这样走了，然后新的slot的时间必然是64秒之前的，然后你以为时间不对，然后就又去增加slot···
        slot++;
    }

	//k 信号处理调用这个函数却没有像下面一样更新当前最新槽位的时间,
	//k 这样迫使在下次ngx_time_update的时候看这个槽位上还是64秒之前的那个时间，于是将slot增加1，放入下一个。这样这个槽位就相当于白费了。
	//tp->sec = sec;//k 更新新的槽位的时间，让他等于最新的时间
/*k 上面这行曾经有个bug,见这里:http://forum.nginx.org/read.php?29,231001
*) Fixed possible use of old cached times if runtime went backwards.

If ngx_time_sigsafe_update() updated only ngx_cached_err_log_time, and
then clock was adjusted backwards, the cached_time[slot].sec might
accidentally match current seconds on next ngx_time_update() call,
resulting in various cached times not being updated.

Fix is to clear the cached_time[slot].sec to explicitly mark cached times
are stale and need updating.

*/
    ngx_gmtime(sec + cached_gmtoff * 60, &tm);

    p = &cached_err_log_time[slot][0];

    (void) ngx_sprintf(p, "%4d/%02d/%02d %02d:%02d:%02d",
                       tm.ngx_tm_year, tm.ngx_tm_mon,
                       tm.ngx_tm_mday, tm.ngx_tm_hour,
                       tm.ngx_tm_min, tm.ngx_tm_sec);

    ngx_memory_barrier();
//k 其实在这里我们知道，ngx_cached_* 这几个变量，不一定是一一的对应到对应的数组的slot位置，一旦掉了这个函数，ngx_cached_err_log_time就指向最新的slot处；
//k 但其他却指向前一个，然后等ngx_time_update调用后，全都指向再下一个，相当于一个信号让他们跳级了
    ngx_cached_err_log_time.data = p;

    ngx_unlock(&ngx_time_lock);
}

#endif


u_char *
ngx_http_time(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    ngx_gmtime(t, &tm);

    return ngx_sprintf(buf, "%s, %02d %s %4d %02d:%02d:%02d GMT",
                       week[tm.ngx_tm_wday],
                       tm.ngx_tm_mday,
                       months[tm.ngx_tm_mon - 1],
                       tm.ngx_tm_year,
                       tm.ngx_tm_hour,
                       tm.ngx_tm_min,
                       tm.ngx_tm_sec);
}


u_char *
ngx_http_cookie_time(u_char *buf, time_t t)
{
    ngx_tm_t  tm;

    ngx_gmtime(t, &tm);

    /*
     * Netscape 3.x does not understand 4-digit years at all and
     * 2-digit years more than "37"
     */

    return ngx_sprintf(buf,
                       (tm.ngx_tm_year > 2037) ?
                                         "%s, %02d-%s-%d %02d:%02d:%02d GMT":
                                         "%s, %02d-%s-%02d %02d:%02d:%02d GMT",
                       week[tm.ngx_tm_wday],
                       tm.ngx_tm_mday,
                       months[tm.ngx_tm_mon - 1],
                       (tm.ngx_tm_year > 2037) ? tm.ngx_tm_year:
                                                 tm.ngx_tm_year % 100,
                       tm.ngx_tm_hour,
                       tm.ngx_tm_min,
                       tm.ngx_tm_sec);
}


void
ngx_gmtime(time_t t, ngx_tm_t *tp)
{
    ngx_int_t   yday;
    ngx_uint_t  n, sec, min, hour, mday, mon, year, wday, days, leap;

    /* the calculation is valid for positive time_t only */

    n = (ngx_uint_t) t;

    days = n / 86400;

    /* Jaunary 1, 1970 was Thursday */

    wday = (4 + days) % 7;

    n %= 86400;
    hour = n / 3600;
    n %= 3600;
    min = n / 60;
    sec = n % 60;

    /*
     * the algorithm based on Gauss' formula,
     * see src/http/ngx_http_parse_time.c
     */

    /* days since March 1, 1 BC */
    days = days - (31 + 28) + 719527;

    /*
     * The "days" should be adjusted to 1 only, however, some March 1st's go
     * to previous year, so we adjust them to 2.  This causes also shift of the
     * last Feburary days to next year, but we catch the case when "yday"
     * becomes negative.
     */

    year = (days + 2) * 400 / (365 * 400 + 100 - 4 + 1);

    yday = days - (365 * year + year / 4 - year / 100 + year / 400);

    if (yday < 0) {
        leap = (year % 4 == 0) && (year % 100 || (year % 400 == 0));
        yday = 365 + leap + yday;
        year--;
    }

    /*
     * The empirical formula that maps "yday" to month.
     * There are at least 10 variants, some of them are:
     *     mon = (yday + 31) * 15 / 459
     *     mon = (yday + 31) * 17 / 520
     *     mon = (yday + 31) * 20 / 612
     */

    mon = (yday + 31) * 10 / 306;

    /* the Gauss' formula that evaluates days before the month */

    mday = yday - (367 * mon / 12 - 30) + 1;

    if (yday >= 306) {

        year++;
        mon -= 10;

        /*
         * there is no "yday" in Win32 SYSTEMTIME
         *
         * yday -= 306;
         */

    } else {

        mon += 2;

        /*
         * there is no "yday" in Win32 SYSTEMTIME
         *
         * yday += 31 + 28 + leap;
         */
    }

    tp->ngx_tm_sec = (ngx_tm_sec_t) sec;
    tp->ngx_tm_min = (ngx_tm_min_t) min;
    tp->ngx_tm_hour = (ngx_tm_hour_t) hour;
    tp->ngx_tm_mday = (ngx_tm_mday_t) mday;
    tp->ngx_tm_mon = (ngx_tm_mon_t) mon;
    tp->ngx_tm_year = (ngx_tm_year_t) year;
    tp->ngx_tm_wday = (ngx_tm_wday_t) wday;
}


time_t
ngx_next_time(time_t when)
{
    time_t     now, next;
    struct tm  tm;

    now = ngx_time();

    ngx_libc_localtime(now, &tm);

    tm.tm_hour = (int) (when / 3600);
    when %= 3600;
    tm.tm_min = (int) (when / 60);
    tm.tm_sec = (int) (when % 60);

    next = mktime(&tm);

    if (next == -1) {
        return -1;
    }

    if (next - now > 0) {
        return next;
    }

    tm.tm_mday++;

    /* mktime() should normalize a date (Jan 32, etc) */

    next = mktime(&tm);

    if (next != -1) {
        return next;
    }

    return -1;
}
