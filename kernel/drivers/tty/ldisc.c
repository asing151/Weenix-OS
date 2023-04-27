#include "drivers/tty/ldisc.h"
#include <drivers/keyboard.h>
#include <drivers/tty/tty.h>
#include <errno.h>
#include <util/bits.h>
#include <util/debug.h>
#include <util/string.h>

#define ldisc_to_tty(ldisc) CONTAINER_OF((ldisc), tty_t, tty_ldisc)

/**
 * Initialize the line discipline. Don't forget to wipe the buffer associated
 * with the line discipline clean.
 *
 * @param ldisc line discipline.
 */
void ldisc_init(ldisc_t *ldisc)
{
    ldisc->ldisc_cooked = 0; /// is this all to be done to wipe clean?
    ldisc->ldisc_tail = 0;
    ldisc->ldisc_head = 0;
    ldisc->ldisc_full = 0;
    sched_queue_init(&ldisc->ldisc_read_queue);
    memset(ldisc->ldisc_buffer, 0, LDISC_BUFFER_SIZE);
    //ldisc->ldisc_buffer[0] = '\0'; /// correct? should I make a new char buffer?
    //NOT_YET_IMPLEMENTED("DRIVERS: ldisc_init");
}

/**
 * While there are no new characters to be read from the line discipline's
 * buffer, you should make the current thread to sleep on the line discipline's
 * read queue. Note that this sleep can be cancelled. What conditions must be met 
 * for there to be no characters to be read?
 *
 * @param  ldisc the line discipline
 * @param  lock  the lock associated with `ldisc`
 * @return       0 if there are new characters to be read or the ldisc is full.
 *               If the sleep was interrupted, return what
 *               `sched_cancellable_sleep_on` returned (i.e. -EINTR)
 */
long ldisc_wait_read(ldisc_t *ldisc, spinlock_t *lock)
{
    long ret;
    while (ldisc->ldisc_cooked == ldisc->ldisc_tail && ldisc->ldisc_full == 0) { /// is this all? are my conditions right? should I check readq?
        ret = sched_cancellable_sleep_on(&ldisc->ldisc_read_queue, lock);
        if (ret != 0){
            return ret;
        }
    }
    return ret;
    //NOT_YET_IMPLEMENTED("DRIVERS: ldisc_wait_read");
    //return -1;
}

/**
 * Reads `count` bytes (at max) from the line discipline's buffer into the
 * provided buffer. Keep in mind the the ldisc's buffer is circular.
 *
 * If you encounter a new line symbol before you have read `count` bytes, you
 * should stop copying and return the bytes read until now.
 * 
 * If you encounter an `EOT` you should stop reading and you should NOT include 
 * the `EOT` in the count of the number of bytes read
 *
 * @param  ldisc the line discipline
 * @param  buf   the buffer to read into.
 * @param  count the maximum number of bytes to read from ldisc.
 * @return       the number of bytes read from the ldisc.
 */
size_t ldisc_read(ldisc_t *ldisc, char *buf, size_t count)
{
    size_t i = 0;
    while (i < count && ldisc->ldisc_cooked != ldisc->ldisc_tail) { /// is the while condition correct?
        if (ldisc->ldisc_buffer[ldisc->ldisc_cooked] == '\n') {
            break;
        }
        if (ldisc->ldisc_buffer[ldisc->ldisc_cooked] == EOT) {
            break;
        }
        buf[i] = ldisc->ldisc_buffer[ldisc->ldisc_cooked];
        ldisc->ldisc_cooked = (ldisc->ldisc_cooked + 1) % LDISC_BUFFER_SIZE; /// do I need these % LDISC_BUFFER_SIZEs?
        i++; /// might be off by 1 or so
    }
    return i;
    //NOT_YET_IMPLEMENTED("DRIVERS: ldisc_read");

}

/**
 * Place the character received into the ldisc's buffer. You should also update
 * relevant fields of the struct.
 *
 * An easier way of handling new characters is making sure that you always have
 * one byte left in the line discipline. This way, if the new character you
 * received is a new line symbol (user hit enter), you can still place the new
 * line symbol into the buffer; if the new character is not a new line symbol,
 * you shouldn't place it into the buffer so that you can leave the space for
 * a new line symbol in the future. 
 * 
 * If the line discipline is full, all incoming characters should be ignored. 
 *
 * Here are some special cases to consider:
 *      1. If the character is a backspace:
 *          * if there is a character to remove you must also emit a `\b` to
 *            the vterminal.
 *      2. If the character is end of transmission (EOT) character (typing ctrl-d)
 *      3. If the character is end of text (ETX) character (typing ctrl-c)
 *      4. If your buffer is almost full and what you received is not a new line
 *      symbol
 *
 * If you did receive a new line symbol, you should wake up the thread that is
 * sleeping on the wait queue of the line discipline. You should also
 * emit a `\n` to the vterminal by using `vterminal_write`.  
 * 
 * If you encounter the `EOT` character, you should add it to the buffer, 
 * cook the buffer, and wake up the reader (but do not emit an `\n` character 
 * to the vterminal)
 * 
 * In case of `ETX` you should cause the input line to be effectively transformed
 * into a cooked blank line. You should clear uncooked portion of the line, by 
 * adjusting ldisc_head. 
 *
 * Finally, if the none of the above cases apply you should fallback to
 * `vterminal_key_pressed`.
 *
 * Don't forget to write the corresponding characters to the virtual terminal
 * when it applies!
 *
 * @param ldisc the line discipline
 * @param c     the new character
 */
void ldisc_key_pressed(ldisc_t *ldisc, char c)
{
    if (ldisc->ldisc_full == 1) {
        return;
    }
    if (c == '\b') {
        if (ldisc->ldisc_head != ldisc->ldisc_tail) { /// do I need this if wrapper?
            ldisc->ldisc_head = (ldisc->ldisc_head - 1) % LDISC_BUFFER_SIZE; /// the %s not needed right
            //vterminal_key_pressed(c); /// how many calls to this?
            vterminal_write(&ldisc_to_tty(ldisc)->tty_vterminal, "\v", 1);
        }
    }
    else if (c == EOT) { /// "do not emit an `\n` character* to the vterminal" - handled right?
        ldisc->ldisc_buffer[ldisc->ldisc_head] = c;
        ldisc->ldisc_head = (ldisc->ldisc_head + 1) % LDISC_BUFFER_SIZE;
        ldisc->ldisc_full = 1;
        ldisc->ldisc_cooked = ldisc->ldisc_tail; /// relationship between cooked and tail?
        sched_wakeup_on(&ldisc->ldisc_read_queue, NULL); /// second arg correct?
    }
    else if (c == ETX) {
        ldisc->ldisc_head = ldisc->ldisc_tail;
    }
    else if (c == '\n') {
        ldisc->ldisc_buffer[ldisc->ldisc_head] = c;
        ldisc->ldisc_head = (ldisc->ldisc_head + 1) % LDISC_BUFFER_SIZE;
        ldisc->ldisc_cooked = ldisc->ldisc_tail;
        vterminal_write(&ldisc_to_tty(ldisc)->tty_vterminal, "\n", 1); /// what is the terminal?
        sched_wakeup_on(&ldisc->ldisc_read_queue, NULL);
    }
    else if (ldisc->ldisc_head == (ldisc->ldisc_tail - 1) % LDISC_BUFFER_SIZE) {
        return;
    }
    else {
        ldisc->ldisc_buffer[ldisc->ldisc_head] = c;
        ldisc->ldisc_head = (ldisc->ldisc_head + 1) % LDISC_BUFFER_SIZE; /// are thesea the only steps?
        vterminal_key_pressed(&ldisc_to_tty(ldisc)->tty_vterminal); /// what args???
    }
    /// NOT_YET_IMPLEMENTED("DRIVERS: ldisc_key_pressed");
}

/**
 * Copy the raw part of the line discipline buffer into the buffer provided.
 *
 * @param  ldisc the line discipline
 * @param  s     the character buffer to write to
 * @return       the number of bytes copied
 */
size_t ldisc_get_current_line_raw(ldisc_t *ldisc, char *s)
{
    size_t i = 0;
    while (ldisc->ldisc_tail != ldisc->ldisc_head) { /// what should the check be???
        s[i] = ldisc->ldisc_buffer[ldisc->ldisc_tail];
        ldisc->ldisc_tail = (ldisc->ldisc_tail + 1) % LDISC_BUFFER_SIZE;
        i++;
    }
    NOT_YET_IMPLEMENTED("DRIVERS: ldisc_get_current_line_raw");
    return 0;
}
