#include "vctrs.h"
#include "utils.h"
#include "subscript-loc.h"

static SEXP int_invert_location(SEXP subscript, R_len_t n,
                                const struct vec_as_location_opts* opts);
static SEXP int_filter_zero(SEXP subscript, R_len_t n_zero);
static void int_check_consecutive(SEXP subscript, R_len_t n,
                                  const struct vec_as_location_opts* opts);

static void stop_subscript_missing(SEXP i);
static void stop_subscript_oob_location(SEXP i, R_len_t size,
                                        const struct vec_as_location_opts* opts);
static void stop_subscript_oob_name(SEXP i, SEXP names,
                                    const struct vec_as_location_opts* opts);
static void stop_location_negative(SEXP i,
                                   const struct vec_as_location_opts* opts);
static void stop_indicator_size(SEXP i, SEXP n,
                                const struct vec_as_location_opts* opts);
static void stop_location_negative_missing(SEXP i,
                                           const struct vec_as_location_opts* opts);
static void stop_location_negative_positive(SEXP i,
                                            const struct vec_as_location_opts* opts);
static void stop_location_oob_non_consecutive(SEXP i, R_len_t size,
                                              const struct vec_as_location_opts* opts);


static SEXP int_as_location(SEXP subscript, R_len_t n,
                            const struct vec_as_location_opts* opts) {
  const int* data = INTEGER_RO(subscript);
  R_len_t loc_n = Rf_length(subscript);

  // Zeros need to be filtered out from the subscript vector.
  // `int_invert_location()` filters them out for negative indices, but
  // positive indices need to go through and `int_filter_zero()`.
  R_len_t n_zero = 0;

  bool extended = false;

  for (R_len_t i = 0; i < loc_n; ++i, ++data) {
    int elt = *data;

    if (elt == NA_INTEGER) {
      if (opts->missing == SUBSCRIPT_MISSING_ERROR) {
        stop_subscript_missing(subscript);
      }
    } else {
      if (elt < 0) {
        switch (opts->loc_negative) {
        case LOC_NEGATIVE_INVERT: return int_invert_location(subscript, n, opts);
        case LOC_NEGATIVE_ERROR: stop_location_negative(subscript, opts);
        case LOC_NEGATIVE_IGNORE: break;
        }
      }

      if (elt == 0) {
        ++n_zero;
      } else if (abs(elt) > n) {
        if (opts->loc_oob == LOC_OOB_ERROR) {
          stop_subscript_oob_location(subscript, n, opts);
        }
        extended = true;
      }
    }
  }

  if (n_zero) {
    subscript = int_filter_zero(subscript, n_zero);
  }
  PROTECT(subscript);

  if (extended) {
    int_check_consecutive(subscript, n, opts);
  }

  UNPROTECT(1);
  return subscript;
}


static SEXP lgl_as_location(SEXP subscript, R_len_t n,
                            const struct vec_as_location_opts* opts);

static SEXP int_invert_location(SEXP subscript, R_len_t n,
                                const struct vec_as_location_opts* opts) {
  const int* data = INTEGER_RO(subscript);
  R_len_t loc_n = Rf_length(subscript);

  SEXP sel = PROTECT(Rf_allocVector(LGLSXP, n));
  r_lgl_fill(sel, 1, n);

  int* sel_data = LOGICAL(sel);

  for (R_len_t i = 0; i < loc_n; ++i, ++data) {
    int j = *data;

    if (j == NA_INTEGER) {
      stop_location_negative_missing(subscript, opts);
    }
    if (j >= 0) {
      if (j == 0) {
        continue;
      } else {
        stop_location_negative_positive(subscript, opts);
      }
    }

    j = -j;
    if (j > n) {
      struct vec_as_location_opts updated_opts = *opts;
      updated_opts.action = SUBSCRIPT_ACTION_NEGATE;
      stop_subscript_oob_location(subscript, n, &updated_opts);
    }

    sel_data[j - 1] = 0;
  }

  SEXP out = lgl_as_location(sel, n, opts);

  UNPROTECT(1);
  return out;
}

static SEXP int_filter_zero(SEXP subscript, R_len_t n_zero) {
  R_len_t loc_n = vec_size(subscript);
  const int* data = INTEGER_RO(subscript);

  SEXP out = PROTECT(Rf_allocVector(INTSXP, loc_n - n_zero));
  int* out_data = INTEGER(out);

  for (R_len_t i = 0; i < loc_n; ++i, ++data) {
    int elt = *data;
    if (elt != 0) {
      *out_data = elt;
      ++out_data;
    }
  }

  UNPROTECT(1);
  return out;
}

// From compare.c
int qsort_icmp(const void* x, const void* y);

static void int_check_consecutive(SEXP subscript, R_len_t n,
                                  const struct vec_as_location_opts* opts) {
  SEXP sorted = PROTECT(Rf_duplicate(subscript));
  int* p_sorted = INTEGER(sorted);

  R_len_t n_subscript = Rf_length(sorted);
  R_len_t n_missing = 0;

  qsort(p_sorted, n_subscript, sizeof(int), &qsort_icmp);

  for (R_len_t i = 0; i < n_subscript; ++i) {
    int elt = p_sorted[i];

    // All missing values are sorted to the beginning
    if (elt == NA_INTEGER) {
      ++n_missing;
      continue;
    }
    --elt;

    if (elt < n) {
      continue;
    }
    if (elt != (i - n_missing) && elt != n) {
      stop_location_oob_non_consecutive(subscript, n, opts);
    }
  }

  UNPROTECT(1);
}

static SEXP dbl_as_location(SEXP subscript, R_len_t n,
                            const struct vec_as_location_opts* opts) {
  subscript = PROTECT(vec_cast(subscript, vctrs_shared_empty_int, args_empty, args_empty));
  subscript = int_as_location(subscript, n, opts);

  UNPROTECT(1);
  return subscript;
}

static SEXP lgl_as_location(SEXP subscript, R_len_t n,
                            const struct vec_as_location_opts* opts) {
  R_len_t subscript_n = Rf_length(subscript);

  if (subscript_n == n) {
    SEXP out = PROTECT(r_lgl_which(subscript, true));

    SEXP nms = PROTECT(r_names(subscript));
    if (nms != R_NilValue) {
      nms = PROTECT(vec_slice(nms, out));
      r_poke_names(out, nms);
      UNPROTECT(1);
    }

    UNPROTECT(2);
    return out;
  }

  /* A single `TRUE` or `FALSE` index is recycled_nms to the full vector
   * size. This means `TRUE` is synonym for missing index (subscript.e. no
   * subsetting) and `FALSE` is synonym for empty index.
   *
   * We could return the missing argument as sentinel to avoid
   * materialising the index vector for the `TRUE` case but this would
   * make `vec_as_location()` an option type just to optimise a rather
   * uncommon case.
   */
  if (subscript_n == 1) {
    int elt = LOGICAL(subscript)[0];

    SEXP out;
    if (elt == NA_LOGICAL) {
      out = PROTECT(Rf_allocVector(INTSXP, n));
      r_int_fill(out, NA_INTEGER, n);
    } else if (elt) {
      out = PROTECT(Rf_allocVector(INTSXP, n));
      r_int_fill_seq(out, 1, n);
    } else {
      return vctrs_shared_empty_int;
    }

    SEXP nms = PROTECT(r_names(subscript));
    if (nms != R_NilValue) {
      SEXP recycled_nms = PROTECT(Rf_allocVector(STRSXP, n));
      r_chr_fill(recycled_nms, r_chr_get(nms, 0), n);
      r_poke_names(out, recycled_nms);
      UNPROTECT(1);
    }

    UNPROTECT(2);
    return out;
  }

  SEXP n_obj = PROTECT(Rf_ScalarInteger(n));
  stop_indicator_size(subscript, n_obj, opts);

  never_reached("lgl_as_location");
}

static SEXP chr_as_location(SEXP subscript, SEXP names,
                            const struct vec_as_location_opts* opts) {
  if (names == R_NilValue) {
    Rf_errorcall(R_NilValue, "Can't use character names to index an unnamed vector.");
  }
  if (TYPEOF(names) != STRSXP) {
    Rf_errorcall(R_NilValue, "`names` must be a character vector.");
  }

  SEXP matched = PROTECT(Rf_match(names, subscript, NA_INTEGER));

  R_len_t n = Rf_length(matched);
  const int* p = INTEGER_RO(matched);
  const SEXP* ip = STRING_PTR_RO(subscript);

  for (R_len_t k = 0; k < n; ++k) {
    if (p[k] == NA_INTEGER && ip[k] != NA_STRING) {
      stop_subscript_oob_name(subscript, names, opts);
    }
  }

  r_poke_names(matched, PROTECT(r_names(subscript))); UNPROTECT(1);

  UNPROTECT(1);
  return matched;
}

SEXP vec_as_location(SEXP subscript, R_len_t n, SEXP names) {
  return vec_as_location_opts(subscript,
                              n,
                              names,
                              vec_as_location_default_opts);
}

SEXP vec_as_location_opts(SEXP subscript, R_len_t n, SEXP names,
                          const struct vec_as_location_opts* opts) {

  if (vec_dim_n(subscript) != 1) {
    Rf_errorcall(R_NilValue, "`i` must have one dimension, not %d.", vec_dim_n(subscript));
  }

  switch (TYPEOF(subscript)) {
  case NILSXP: return vctrs_shared_empty_int;
  case INTSXP: return int_as_location(subscript, n, opts);
  case REALSXP: return dbl_as_location(subscript, n, opts);
  case LGLSXP: return lgl_as_location(subscript, n, opts);
  case STRSXP: return chr_as_location(subscript, names, opts);

  default: Rf_errorcall(R_NilValue, "`i` must be an integer, character, or logical vector, not a %s.",
                        Rf_type2char(TYPEOF(subscript)));
  }
}

static void stop_subscript_arg_missing() {
  Rf_errorcall(R_NilValue, "`missing` must be one of \"propagate\" or \"error\".");
}
static void stop_bad_negative() {
  Rf_errorcall(R_NilValue, "`negative` must be one of \"invert\", \"error\", or \"ignore\".");
}
static void stop_bad_oob() {
  Rf_errorcall(R_NilValue, "`oob` must be one of \"error\" or \"extend\".");
}

static enum subscript_missing parse_subscript_arg_missing(SEXP x) {
  if (TYPEOF(x) != STRSXP || Rf_length(x) == 0) {
    stop_subscript_arg_missing();
  }

  const char* str = CHAR(STRING_ELT(x, 0));

  if (!strcmp(str, "propagate")) return SUBSCRIPT_MISSING_PROPAGATE;
  if (!strcmp(str, "error")) return SUBSCRIPT_MISSING_ERROR;
  stop_subscript_arg_missing();

  never_reached("stop_subscript_arg_missing");
}
static enum num_as_location_loc_negative parse_loc_negative(SEXP x) {
  if (TYPEOF(x) != STRSXP || Rf_length(x) == 0) {
    stop_bad_negative();
  }

  const char* str = CHAR(STRING_ELT(x, 0));

  if (!strcmp(str, "invert")) return LOC_NEGATIVE_INVERT;
  if (!strcmp(str, "error")) return LOC_NEGATIVE_ERROR;
  if (!strcmp(str, "ignore")) return LOC_NEGATIVE_IGNORE;
  stop_bad_negative();

  never_reached("stop_bad_negative");
}
static enum num_as_location_loc_oob parse_loc_oob(SEXP x) {
  if (TYPEOF(x) != STRSXP || Rf_length(x) == 0) {
    stop_bad_oob();
  }

  const char* str = CHAR(STRING_ELT(x, 0));

  if (!strcmp(str, "error")) return LOC_OOB_ERROR;
  if (!strcmp(str, "extend")) return LOC_OOB_EXTEND;
  stop_bad_oob();

  never_reached("stop_bad_oob");
}

SEXP vctrs_as_location(SEXP subscript, SEXP n_, SEXP names,
                       SEXP loc_negative, SEXP loc_oob,
                       SEXP missing, SEXP arg) {
  R_len_t n = 0;

  if (n_ == R_NilValue && TYPEOF(subscript) == STRSXP) {
    n = Rf_length(subscript);
  } else {
    if (OBJECT(n_) || TYPEOF(n_) != INTSXP) {
      n_ = vec_coercible_cast(n_, vctrs_shared_empty_int, args_empty, args_empty);
    }
    PROTECT(n_);

    if (Rf_length(n_) != 1) {
      Rf_error("Internal error: `n` must be a scalar number");
    }

    n = r_int_get(n_, 0);
    UNPROTECT(1);
  }

  struct vec_as_location_opts opts = {
    .action = SUBSCRIPT_ACTION_DEFAULT,
    .missing = parse_subscript_arg_missing(missing),
    .loc_negative = parse_loc_negative(loc_negative),
    .loc_oob = parse_loc_oob(loc_oob),
    .subscript_arg = arg
  };

  return vec_as_location_opts(subscript, n, names, &opts);
}

static void stop_subscript_missing(SEXP i) {
  vctrs_eval_mask1(Rf_install("stop_subscript_missing"),
                   syms_i, i,
                   vctrs_ns_env);
  never_reached("stop_subscript_missing");
}

static void stop_location_negative_missing(SEXP i,
                                           const struct vec_as_location_opts* opts) {
  vctrs_eval_mask3(Rf_install("stop_location_negative_missing"),
                   syms_i, i,
                   syms_subscript_arg, opts->subscript_arg,
                   syms_subscript_action, get_opts_action(opts),
                   vctrs_ns_env);
  never_reached("stop_location_negative_missing");
}
static void stop_location_negative_positive(SEXP i,
                                            const struct vec_as_location_opts* opts) {
  vctrs_eval_mask3(Rf_install("stop_location_negative_positive"),
                   syms_i, i,
                   syms_subscript_arg, opts->subscript_arg,
                   syms_subscript_action, get_opts_action(opts),
                   vctrs_ns_env);
  never_reached("stop_location_negative_positive");
}

static void stop_subscript_oob_location(SEXP i, R_len_t size,
                                        const struct vec_as_location_opts* opts) {
  SEXP size_obj = PROTECT(r_int(size));
  vctrs_eval_mask5(Rf_install("stop_subscript_oob"),
                   syms_i, i,
                   syms_subscript_type, chrs_numeric,
                   syms_size, size_obj,
                   syms_subscript_action, get_opts_action(opts),
                   syms_subscript_arg, opts->subscript_arg,
                   vctrs_ns_env);

  UNPROTECT(1);
  never_reached("stop_subscript_oob_location");
}
static void stop_subscript_oob_name(SEXP i, SEXP names,
                                    const struct vec_as_location_opts* opts) {
  vctrs_eval_mask5(Rf_install("stop_subscript_oob"),
                   syms_i, i,
                   syms_subscript_type, chrs_character,
                   syms_names, names,
                   syms_subscript_action, get_opts_action(opts),
                   syms_subscript_arg, opts->subscript_arg,
                   vctrs_ns_env);
  never_reached("stop_subscript_oob_name");
}

static void stop_location_negative(SEXP i,
                                   const struct vec_as_location_opts* opts) {
  vctrs_eval_mask3(Rf_install("stop_location_negative"),
                   syms_i, i,
                   syms_subscript_action, get_opts_action(opts),
                   syms_subscript_arg, opts->subscript_arg,
                   vctrs_ns_env);
  never_reached("stop_location_negative");
}

static void stop_indicator_size(SEXP i, SEXP n,
                                const struct vec_as_location_opts* opts) {
  vctrs_eval_mask4(Rf_install("stop_indicator_size"),
                   syms_i, i,
                   syms_n, n,
                   syms_subscript_action, get_opts_action(opts),
                   syms_subscript_arg, opts->subscript_arg,
                   vctrs_ns_env);
  never_reached("stop_indicator_size");
}

static void stop_location_oob_non_consecutive(SEXP i, R_len_t size,
                                              const struct vec_as_location_opts* opts) {
  SEXP size_obj = PROTECT(r_int(size));
  vctrs_eval_mask4(Rf_install("stop_location_oob_non_consecutive"),
                   syms_i, i,
                   syms_size, size_obj,
                   syms_subscript_action, get_opts_action(opts),
                   syms_subscript_arg, opts->subscript_arg,
                   vctrs_ns_env);

  UNPROTECT(1);
  never_reached("stop_location_oob_non_consecutive");
}


struct vec_as_location_opts vec_as_location_default_opts_obj;
struct vec_as_location_opts vec_as_location_default_assign_opts_obj;

void vctrs_init_subscript_loc(SEXP ns) {
  vec_as_location_default_opts_obj.action = SUBSCRIPT_ACTION_DEFAULT;
  vec_as_location_default_opts_obj.loc_negative = LOC_NEGATIVE_INVERT;
  vec_as_location_default_opts_obj.loc_oob = LOC_OOB_ERROR;
  vec_as_location_default_opts_obj.subscript_arg = R_NilValue;
  vec_as_location_default_opts_obj.missing = SUBSCRIPT_MISSING_PROPAGATE;

  vec_as_location_default_assign_opts_obj.action = SUBSCRIPT_ACTION_ASSIGN;
  vec_as_location_default_assign_opts_obj.loc_negative = LOC_NEGATIVE_INVERT;
  vec_as_location_default_assign_opts_obj.loc_oob = LOC_OOB_ERROR;
  vec_as_location_default_assign_opts_obj.subscript_arg = R_NilValue;
  vec_as_location_default_assign_opts_obj.missing = SUBSCRIPT_MISSING_PROPAGATE;
}
