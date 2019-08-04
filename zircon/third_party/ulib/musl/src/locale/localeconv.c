#include <limits.h>
#include <locale.h>

static const struct lconv posix_lconv = {
    .decimal_point = (char*)".",
    .thousands_sep = (char*)"",
    .grouping = (char*)"",
    .int_curr_symbol = (char*)"",
    .currency_symbol = (char*)"",
    .mon_decimal_point = (char*)"",
    .mon_thousands_sep = (char*)"",
    .mon_grouping = (char*)"",
    .positive_sign = (char*)"",
    .negative_sign = (char*)"",
    .int_frac_digits = CHAR_MAX,
    .frac_digits = CHAR_MAX,
    .p_cs_precedes = CHAR_MAX,
    .p_sep_by_space = CHAR_MAX,
    .n_cs_precedes = CHAR_MAX,
    .n_sep_by_space = CHAR_MAX,
    .p_sign_posn = CHAR_MAX,
    .n_sign_posn = CHAR_MAX,
    .int_p_cs_precedes = CHAR_MAX,
    .int_p_sep_by_space = CHAR_MAX,
    .int_n_cs_precedes = CHAR_MAX,
    .int_n_sep_by_space = CHAR_MAX,
    .int_p_sign_posn = CHAR_MAX,
    .int_n_sign_posn = CHAR_MAX,
};

struct lconv* localeconv(void) {
  return (void*)&posix_lconv;
}
