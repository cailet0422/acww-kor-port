/* The sequencer's pitch table (port/src/pitch_table.c) against its definition: entry i is
 * 2^(i/768) - 1 as a 16-bit fraction. Checked: the first and last entries, that it rises all
 * the way, and that every entry is the definition's number to within one unit (the floating
 * point of the machine the check runs on may round the last bit otherwise).
 *
 *     acww-pitch-test
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern const uint16_t port_pitch_table[768];

int main(void)
{
    int failures = 0;
    if (port_pitch_table[0] != 0 || port_pitch_table[767] != 0xff8a) {
        printf("pitch-test: FAILED: the table's ends are %u and %u\n", port_pitch_table[0], port_pitch_table[767]);
        failures++;
    }
    for (int i = 0; i < 768; i++) {
        double exact = (pow(2.0, i / 768.0) - 1.0) * 65536.0;
        if (fabs(port_pitch_table[i] - exact) > 1.0 || (i > 0 && port_pitch_table[i] <= port_pitch_table[i - 1])) {
            printf("pitch-test: FAILED: entry %d is %u, the definition gives %.2f\n", i, port_pitch_table[i], exact);
            failures++;
        }
    }
    /* a semitone is 64 steps: seven of them up is a fifth, a frequency 1.4983 times as high */
    double fifth = 1.0 + port_pitch_table[7 * 64] / 65536.0;
    if (fabs(fifth - 1.498307) > 0.0001) {
        printf("pitch-test: FAILED: a fifth is %.6f times the frequency\n", fifth);
        failures++;
    }
    printf(failures ? "pitch-test: %d failures\n" : "pitch-test: passed\n", failures);
    return failures != 0;
}
