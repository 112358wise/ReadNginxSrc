
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
//k �������һ��û�����ף�ʱ����࣬д�١�������ֳ�ͻ���϶����м���ռ�˲���NGX_TIME_SLOTS�뻹û�п�ʼ����
//k �������ˣ��������˫����ĳ����汾��64������ƣ�ʱ����ַ���ֵ����64��һ���ֻأ�
//k ͨ�������ngx_cached_*����ֱ�ӷ��ʣ�������ͨ��cached_err_log_time[slot]�����������ʡ�������ͻ�ĸ��ʷǳ�С��64���ڻ�û�е������У����п��ܡ�
//k ������ʵ�������뵽��ȡ������־ʱ�䣬����ʱ�䣬���߻��ǿ��ܳ���һ���������������һ��Ŀ�ʼ��������ûɶ��ϵ��
#define NGX_TIME_SLOTS   64

static ngx_uint_t        slot;
static ngx_atomic_t      ngx_time_lock;//k ������Ч����һ������

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
ngx_master_process_cycle��ÿ�δ������źź�������
ngx_epoll_process_events���������ngx_timer_resolutionҲ�ᶨ�ڵ������������þ�ȷʱ��ʱ�õ�
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
        return;//k ���û�õ������Ǹ����������ڸ�����
    }

    ngx_gettimeofday(&tv);//k gettimeofday(tp, NULL);

    sec = tv.tv_sec;
    msec = tv.tv_usec / 1000;

    ngx_current_msec = (ngx_msec_t) sec * 1000 + msec;//k ��ǰ�ĺ�������������������

    tp = &cached_time[slot];//k ����ȫ�ֵĻ���ʱ��slotλ��

    if (tp->sec == sec) {
		//k ������ȣ���ֻ���º������ˣ�����������������ʱ��ֵ�أ���Ϊ������ַ���ֵѹ���Ͳ���Ҫ���������������ǿ���ֱ���˳���
        tp->msec = msec;
        ngx_unlock(&ngx_time_lock);
        return;
    }

    if (slot == NGX_TIME_SLOTS - 1) {//k ������ѯ���cached_time�ṹ����ͷ�˻�0
        slot = 0;//k ����ͼ�1�ˣ��������̶߳�ȡ���ֵ����ô��?���һ�û�������أ�������ôҲ��ȡ��һ����
    } else {
        slot++;
    }

    tp = &cached_time[slot];//k ָ����һ����λ

    tp->sec = sec;//k �����µĲ�λ��ʱ�䣬�����������µ�ʱ��
    tp->msec = msec;

    ngx_gmtime(sec, &gmt);//k ��һ���ַ����͵�ʱ��ֵ����
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

	//k ���´�����־��ʱ��ֵ
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


    ngx_memory_barrier();//k ���������ɶ � 
    //k �����ǣ����������������������ܻὫp1,p2,p3������123123�ķ�ʽ���У��Ż�Ϊ112233�ķ�ʽ���Ӷ���Ӧ�ó����ȡ��ʱ�䷢����ͻ�ĸ��ʸ����ˡ���! 
//k ���������ʽ���£����º������ط��Ϳ���ͨ������Ľṹֱ�ӷ��ʵ��ˣ�����ͨ�����飬slot�����ʡ��൱������printf���µ�ʱ�������̻߳��Ƿ��ʲ�����
//k �����൱������ʱ������˫����ĳ����汾��64�����õģ�64��һ���ֻأ�������֤��������£�������ִ��˵����
//k ���������и�Сע��㣬�������ȴ�һ��ngx_cached_http_time��־��Ȼ��һ��ngx_cached_http_log_time���ļ��У�Ȼ���֣�ǰ��һ����10���ʱ�򣬺���һ��ȴ��9�룬��Խ��
//k ��Ϊ����������ĸ��²���ԭ�Ӳ���
    ngx_cached_time = tp;
    ngx_cached_http_time.data = p0;
    ngx_cached_err_log_time.data = p1;
    ngx_cached_http_log_time.data = p2;

    ngx_unlock(&ngx_time_lock);
}


#if !(NGX_WIN32)

void
ngx_time_sigsafe_update(void)
{//k �������ֻ����cached_err_log_timeʱ��,ֻ��ngx_signal_handler�����ù�,�յ��źŵ�ʱ��
//k ȡ������ʱ�䣬����ͨ�������ʱ��ȡ�ġ������õ��˻���������ˣ����Ұ�slot������
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
	
    if (tp->sec == sec) {//k ���������ȣ����ø���ɶ�ġ�����һ���ڶ���źţ������ظ����£�ƾ�װ׷Ѷ��slot
        ngx_unlock(&ngx_time_lock);
        return;
    }

    if (slot == NGX_TIME_SLOTS - 1) {
        slot = 0;
    } else {//k ����һ���µĲ�λ������ȴ������&cached_time[slot]����ʱ��ֵ����������cached_err_log_timeʱ�䣬����֪�����и���
    //k ������һ�������600���źţ���һ�����������ˣ�Ȼ���µ�slot��ʱ���Ȼ��64��֮ǰ�ģ�Ȼ������Ϊʱ�䲻�ԣ�Ȼ�����ȥ����slot������
        slot++;
    }

	//k �źŴ�������������ȴû��������һ�����µ�ǰ���²�λ��ʱ��,
	//k ������ʹ���´�ngx_time_update��ʱ�������λ�ϻ���64��֮ǰ���Ǹ�ʱ�䣬���ǽ�slot����1��������һ�������������λ���൱�ڰ׷��ˡ�
	//tp->sec = sec;//k �����µĲ�λ��ʱ�䣬�����������µ�ʱ��
/*k �������������и�bug,������:http://forum.nginx.org/read.php?29,231001
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
//k ��ʵ����������֪����ngx_cached_* �⼸����������һ����һһ�Ķ�Ӧ����Ӧ�������slotλ�ã�һ���������������ngx_cached_err_log_time��ָ�����µ�slot����
//k ������ȴָ��ǰһ����Ȼ���ngx_time_update���ú�ȫ��ָ������һ�����൱��һ���ź�������������
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
