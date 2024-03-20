#include <stdint.h>

//idea: treat rightmost bits of integer representing a fraction
//17.14 format has x / 2^14 so that there are 17 bits before decimal point and 14 bits after

//let f = 2^q where we use 17.14 format here (q = 17)
#define f ((1 << 14))
//overflow를 검사하기 위해 int 범위에 대한 메크로 설정
#define MAX ((1 << 31) - 1)
#define MIN (-(1 << 31))

int convert_to_fp(int n);
int convert_to_int_zero(int x);
int convert_to_int_nearest(int x);
int add_two_fp(int x, int y);
int subtract_two_fp(int x, int y);
int add_fp_int(int x, int n);
int subtract_int_fp(int n, int x);
int multiply_two_fp(int x, int y);
int multiply_int_fp(int x, int n);
int divide_two_fp(int x, int y);
int divide_int_fp(int x, int n);

int 
convert_to_fp(int n) {
    return n * f;
}

int
convert_to_int_zero(int x) {
    return x / f;
}

int
convert_to_int_nearest(int x) {
    if (x >= 0) {
        return (x + f / 2) / f;
    } else {
        return (x - f / 2) / f;
    }
}

int
add_two_fp(int x, int y) {
    return x + y;
}

int
subtract_two_fp(int x, int y) {
    return x - y;
}

int
add_fp_int(int x, int n) {
    return x + n * f;
}

int
subtract_int_fp(int n, int x) {
    return x - n * f;
}

int
multiply_two_fp(int x, int y) {
    return (int)(((int64_t) x) * y / f);
}

int
multiply_int_fp(int x, int n) {
    return x * n;
}

int
divide_two_fp(int x, int y) {
    return (int)(((int64_t) x) * f / y);
}

int
divide_int_fp(int x, int n) {
    return x / n;
}