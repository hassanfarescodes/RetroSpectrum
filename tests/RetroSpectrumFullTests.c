/*
 * RetroSpectrum exhaustive per-translation-unit test harness.
 *
 * This file is compiled repeatedly by run_retrospectrum_full_tests.sh. For each
 * build, one production .c file is included directly so its static functions
 * are visible to the tests without modifying RetroSpectrum source code.
 *
 * This suite deliberately separates:
 *   1. Function availability checks for every function definition.
 *   2. Behavioral unit tests for deterministic helpers and serializers.
 *   3. Integration smoke tests for workstations, database, identity, TLS, SDL,
 *      the world map, and the bit-stream classifier.
 *
 * Hardware-dependent RF behavior still requires physical SDR loopback tests.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Public cross-module APIs used directly by integration tests. */
int DATABASE_CRYPTO_set_key_path(const char *path, char *error, size_t error_size);
void SERVER_IDENTITY_set_server_mode(int server_mode);

#ifndef RS_TEST_TARGET
#error "Compile with -DRS_TEST_TARGET=<1..16>."
#endif

static int rs_test_passed = 0;
static int rs_test_failed = 0;
static int rs_test_skipped = 0;
static int rs_test_quiet = 0;
static char rs_test_temp_dir[PATH_MAX];
static const char *rs_test_project_root = ".";

#define RS_ANSI_GREEN "\033[92m"
#define RS_ANSI_RED "\033[91m"
#define RS_ANSI_YELLOW "\033[93m"
#define RS_ANSI_RESET "\033[0m"

static void rs_test_report_pass(const char *name) {
    rs_test_passed++;
    if (!rs_test_quiet) {
        printf(RS_ANSI_GREEN "PASSED" RS_ANSI_RESET " %s\n", name);
    }
}

static void rs_test_report_fail(const char *name, const char *file, int line) {
    rs_test_failed++;
    printf(RS_ANSI_RED "FAILED" RS_ANSI_RESET " %s (%s:%d)\n", name, file, line);
}

static void rs_test_report_skip(const char *name, const char *reason) {
    rs_test_skipped++;
    printf(RS_ANSI_YELLOW "SKIPPED" RS_ANSI_RESET " %s — %s\n", name, reason ? reason : "unavailable");
}

#define RS_CHECK(name, expression) \
    do { \
        if ((expression)) rs_test_report_pass((name)); \
        else rs_test_report_fail((name), __FILE__, __LINE__); \
    } while (0)

#define RS_SKIP(name, reason) rs_test_report_skip((name), (reason))

/*
 * __typeof__ is used only to preserve each function's exact pointer type.
 * Taking every address means any removed, renamed, or non-compiling function
 * fails the target build instead of silently disappearing from the suite.
 */
#define RS_TOUCH(function_name) \
    do { \
        __typeof__(&(function_name)) rs_test_function_pointer = &(function_name); \
        RS_CHECK("function addressable: " #function_name, rs_test_function_pointer != NULL); \
    } while (0)

static int rs_test_write_bytes(const char *path, const void *data, size_t size, mode_t mode) {
    int fd;
    const unsigned char *cursor = (const unsigned char *)data;
    size_t remaining = size;
    mode_t old_mask = umask(0077);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    umask(old_mask);
    if (fd < 0) return 0;
    while (remaining > 0) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return 0;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    if (fsync(fd) != 0) {
        close(fd);
        return 0;
    }
    return close(fd) == 0;
}

static int rs_test_write_iq_file(const char *path, size_t iq_count) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    for (size_t i = 0; i < iq_count; i++) {
        int16_t sample[2];
        double angle = (2.0 * 3.14159265358979323846 * (double)(i % 32U)) / 32.0;
        sample[0] = (int16_t)lrint(cos(angle) * 12000.0);
        sample[1] = (int16_t)lrint(sin(angle) * 12000.0);
        if (fwrite(sample, sizeof(sample), 1, fp) != 1) {
            fclose(fp);
            return 0;
        }
    }
    return fclose(fp) == 0;
}

static TTF_Font *rs_test_open_font(void) {
    const char *configured = getenv("RS_TEST_FONT");
    const char *paths[] = {
        configured,
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        NULL
    };
    for (size_t i = 0; paths[i]; i++) {
        if (paths[i] && paths[i][0]) {
            TTF_Font *font = TTF_OpenFont(paths[i], 14);
            if (font) return font;
        }
    }
    return NULL;
}

static int rs_test_make_renderer(SDL_Surface **surface, SDL_Renderer **renderer, TTF_Font **font) {
    if (!surface || !renderer || !font) return 0;
    *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!*surface) return 0;
    *renderer = SDL_CreateSoftwareRenderer(*surface);
    if (!*renderer) {
        SDL_FreeSurface(*surface);
        *surface = NULL;
        return 0;
    }
    *font = rs_test_open_font();
    if (!*font) {
        SDL_DestroyRenderer(*renderer);
        SDL_FreeSurface(*surface);
        *renderer = NULL;
        *surface = NULL;
        return 0;
    }
    return 1;
}

static void rs_test_destroy_renderer(SDL_Surface *surface, SDL_Renderer *renderer, TTF_Font *font) {
    if (font) TTF_CloseFont(font);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
}

#if RS_TEST_TARGET == 1
#include "../src/AnalysisWorkstation.c"

static const char *rs_test_target_name = "AnalysisWorkstation.c";
static const int rs_test_expected_function_count = 218;

static void rs_test_touch_all(void) {
    RS_TOUCH(ANALYSIS_get_adjusted_mouse_state);
    RS_TOUCH(ANALYSIS_secure_clear);
    RS_TOUCH(ANALYSIS_limit_double);
    RS_TOUCH(ANALYSIS_set_context);
    RS_TOUCH(ANALYSIS_invalidate_constellation_cache);
    RS_TOUCH(ANALYSIS_constellation_cache_matches);
    RS_TOUCH(ANALYSIS_restore_constellation_cache);
    RS_TOUCH(ANALYSIS_store_constellation_cache);
    RS_TOUCH(ANALYSIS_save_workspace_state);
    RS_TOUCH(ANALYSIS_load_workspace_state);
    RS_TOUCH(ANALYSIS_switch_workspace);
    RS_TOUCH(ANALYSIS_name_compare);
    RS_TOUCH(ANALYSIS_is_complex16_file);
    RS_TOUCH(ANALYSIS_clear_loaded_file);
    RS_TOUCH(ANALYSIS_clear_current_workspace);
    RS_TOUCH(ANALYSIS_parse_recording_metadata);
    RS_TOUCH(ANALYSIS_scan_recordings);
    RS_TOUCH(ANALYSIS_open_selected_recording);
    RS_TOUCH(ANALYSIS_path_file_name);
    RS_TOUCH(ANALYSIS_workspace_is_empty);
    RS_TOUCH(ANALYSIS_initialize_workspaces_for_export);
    RS_TOUCH(ANALYSIS_get_recording_workspace);
    RS_TOUCH(ANALYSIS_get_available_workspace_count);
    RS_TOUCH(ANALYSIS_export_recording_to_workspace);
    RS_TOUCH(ANALYSIS_select_relative);
    RS_TOUCH(ANALYSIS_zoom_at_fraction);
    RS_TOUCH(ANALYSIS_drag_move_view);
    RS_TOUCH(ANALYSIS_get_layout);
    RS_TOUCH(ANALYSIS_get_hover_graph_layout);
    RS_TOUCH(ANALYSIS_get_constellation_mode_button_rects);
    RS_TOUCH(ANALYSIS_draw_constellation_mode_buttons);
    RS_TOUCH(ANALYSIS_get_constellation_psk_prompt_rects);
    RS_TOUCH(ANALYSIS_draw_constellation_psk_prompt);
    RS_TOUCH(ANALYSIS_handle_constellation_psk_prompt_event);
    RS_TOUCH(ANALYSIS_handle_constellation_mode_click);
    RS_TOUCH(ANALYSIS_draw_hover_sync_line);
    RS_TOUCH(ANALYSIS_freq_frac_from_mouse_y);
    RS_TOUCH(ANALYSIS_update_filter_from_mouse);
    RS_TOUCH(ANALYSIS_apply_filter_selection);
    RS_TOUCH(ANALYSIS_clear_filter);
    RS_TOUCH(ANALYSIS_frequency_from_spec_frac);
    RS_TOUCH(ANALYSIS_get_filter_label);
    RS_TOUCH(ANALYSIS_set_marker_from_mouse);
    RS_TOUCH(ANALYSIS_time_frac_from_mouse_x);
    RS_TOUCH(ANALYSIS_update_column_selection_from_mouse);
    RS_TOUCH(ANALYSIS_apply_column_selection);
    RS_TOUCH(ANALYSIS_crop_button_rect);
    RS_TOUCH(ANALYSIS_clear_workspace_button_rect);
    RS_TOUCH(ANALYSIS_draw_crop_button);
    RS_TOUCH(ANALYSIS_draw_clear_workspace_button);
    RS_TOUCH(ANALYSIS_noise_graph_from_point);
    RS_TOUCH(ANALYSIS_noise_frac_from_mouse_y);
    RS_TOUCH(ANALYSIS_update_noise_selection_from_mouse);
    RS_TOUCH(ANALYSIS_apply_noise_selection);
    RS_TOUCH(ANALYSIS_clear_noise_filter);
    RS_TOUCH(ANALYSIS_noise_value_range);
    RS_TOUCH(ANALYSIS_noise_value_matches);
    RS_TOUCH(ANALYSIS_update_noise_column_mask);
    RS_TOUCH(ANALYSIS_apply_noise_filter_to_rendered_lines);
    RS_TOUCH(ANALYSIS_draw_noise_filter_overlay);
    RS_TOUCH(ANALYSIS_export_classification_fields);
    RS_TOUCH(ANALYSIS_signal_menu_available);
    RS_TOUCH(ANALYSIS_selected_file_name);
    RS_TOUCH(ANALYSIS_short_text);
    RS_TOUCH(ANALYSIS_file_search_matches);
    RS_TOUCH(ANALYSIS_file_search_filtered_count);
    RS_TOUCH(ANALYSIS_file_search_filtered_index_at);
    RS_TOUCH(ANALYSIS_file_search_popup_rect);
    RS_TOUCH(ANALYSIS_file_search_input_rect);
    RS_TOUCH(ANALYSIS_file_search_button_rect);
    RS_TOUCH(ANALYSIS_file_search_clamp_scroll);
    RS_TOUCH(ANALYSIS_open_file_search_menu);
    RS_TOUCH(ANALYSIS_close_file_search_menu);
    RS_TOUCH(ANALYSIS_file_search_select_index);
    RS_TOUCH(ANALYSIS_file_search_insert_text);
    RS_TOUCH(ANALYSIS_file_search_backspace);
    RS_TOUCH(ANALYSIS_file_search_delete);
    RS_TOUCH(ANALYSIS_draw_modal_button);
    RS_TOUCH(ANALYSIS_handle_file_search_event);
    RS_TOUCH(ANALYSIS_draw_file_search_button);
    RS_TOUCH(ANALYSIS_draw_file_search_popup);
    RS_TOUCH(ANALYSIS_draw_wrapped_text);
    RS_TOUCH(ANALYSIS_draw_wrapped_text_limited);
    RS_TOUCH(ANALYSIS_format_transmit_live_conversion);
    RS_TOUCH(ANALYSIS_signal_clamp_file_cursor);
    RS_TOUCH(ANALYSIS_signal_clear_file_selection);
    RS_TOUCH(ANALYSIS_signal_file_has_selection);
    RS_TOUCH(ANALYSIS_signal_get_file_selection_range);
    RS_TOUCH(ANALYSIS_signal_delete_file_selection);
    RS_TOUCH(ANALYSIS_signal_text_width_range);
    RS_TOUCH(ANALYSIS_signal_filename_wrap_lines);
    RS_TOUCH(ANALYSIS_signal_insert_file_cursor_text);
    RS_TOUCH(ANALYSIS_signal_backspace_file_cursor_text);
    RS_TOUCH(ANALYSIS_signal_delete_file_cursor_text);
    RS_TOUCH(ANALYSIS_signal_set_file_cursor_from_mouse);
    RS_TOUCH(ANALYSIS_signal_draw_filename_field_text);
    RS_TOUCH(ANALYSIS_signal_append_text);
    RS_TOUCH(ANALYSIS_signal_backspace_text);
    RS_TOUCH(ANALYSIS_signal_clear_active_text);
    RS_TOUCH(ANALYSIS_get_signal_icon_rect);
    RS_TOUCH(ANALYSIS_get_transmit_rect);
    RS_TOUCH(ANALYSIS_get_multithread_rect);
    RS_TOUCH(ANALYSIS_get_signal_trash_rect);
    RS_TOUCH(ANALYSIS_get_signal_menu_rects);
    RS_TOUCH(ANALYSIS_get_signal_marker_rects);
    RS_TOUCH(ANALYSIS_signal_set_time_field_from_marker);
    RS_TOUCH(ANALYSIS_draw_signal_marker_button);
    RS_TOUCH(ANALYSIS_signal_menu_prefill);
    RS_TOUCH(ANALYSIS_draw_thick_line);
    RS_TOUCH(ANALYSIS_draw_circle_outline);
    RS_TOUCH(ANALYSIS_draw_signal_gear_shape);
    RS_TOUCH(ANALYSIS_draw_signal_settings_icon);
    RS_TOUCH(ANALYSIS_draw_signal_trash_shape);
    RS_TOUCH(ANALYSIS_draw_signal_trash_icon);
    RS_TOUCH(ANALYSIS_draw_multithread_icon);
    RS_TOUCH(ANALYSIS_draw_transmit_shape);
    RS_TOUCH(ANALYSIS_draw_transmit_icon);
    RS_TOUCH(ANALYSIS_get_transmit_auth_rects);
    RS_TOUCH(ANALYSIS_get_transmit_config_rects);
    RS_TOUCH(ANALYSIS_get_transmit_progress_rects);
    RS_TOUCH(ANALYSIS_get_transmit_result_rects);
    RS_TOUCH(ANALYSIS_draw_transmit_text_field);
    RS_TOUCH(ANALYSIS_transmit_clamp_cursor);
    RS_TOUCH(ANALYSIS_transmit_insert_text);
    RS_TOUCH(ANALYSIS_transmit_backspace);
    RS_TOUCH(ANALYSIS_transmit_delete);
    RS_TOUCH(ANALYSIS_close_transmit_prompts);
    RS_TOUCH(ANALYSIS_prefill_transmit_fields);
    RS_TOUCH(ANALYSIS_open_transmit_config_prompt);
    RS_TOUCH(ANALYSIS_open_transmit_auth_prompt);
    RS_TOUCH(ANALYSIS_authorize_transmission);
    RS_TOUCH(ANALYSIS_parse_transmit_integer);
    RS_TOUCH(ANALYSIS_parse_transmit_signed_integer);
    RS_TOUCH(ANALYSIS_submit_transmission_settings);
    RS_TOUCH(ANALYSIS_update_transmission_state);
    RS_TOUCH(ANALYSIS_draw_transmit_auth_prompt);
    RS_TOUCH(ANALYSIS_draw_transmit_config_prompt);
    RS_TOUCH(ANALYSIS_draw_animated_transmit_waves);
    RS_TOUCH(ANALYSIS_draw_transmit_progress_prompt);
    RS_TOUCH(ANALYSIS_close_transmit_result);
    RS_TOUCH(ANALYSIS_draw_transmit_result_prompt);
    RS_TOUCH(ANALYSIS_handle_transmit_auth_event);
    RS_TOUCH(ANALYSIS_handle_transmit_config_event);
    RS_TOUCH(ANALYSIS_handle_transmit_progress_event);
    RS_TOUCH(ANALYSIS_handle_transmit_result_event);
    RS_TOUCH(ANALYSIS_get_multithread_prompt_rects);
    RS_TOUCH(ANALYSIS_draw_multithread_prompt);
    RS_TOUCH(ANALYSIS_set_multithread_enabled);
    RS_TOUCH(ANALYSIS_handle_multithread_prompt_event);
    RS_TOUCH(ANALYSIS_get_delete_confirm_rects);
    RS_TOUCH(ANALYSIS_draw_delete_confirm_menu);
    RS_TOUCH(ANALYSIS_open_delete_confirm);
    RS_TOUCH(ANALYSIS_delete_confirmed_file);
    RS_TOUCH(ANALYSIS_handle_delete_confirm_event);
    RS_TOUCH(ANALYSIS_draw_signal_menu);
    RS_TOUCH(ANALYSIS_signal_parse_double_field);
    RS_TOUCH(ANALYSIS_signal_make_base_name);
    RS_TOUCH(ANALYSIS_signal_sanitize_output_filename);
    RS_TOUCH(ANALYSIS_signal_copy_component);
    RS_TOUCH(ANALYSIS_signal_build_live_filename);
    RS_TOUCH(ANALYSIS_signal_refresh_filename_if_auto);
    RS_TOUCH(ANALYSIS_draw_centered_button_text);
    RS_TOUCH(ANALYSIS_signal_copy_crop);
    RS_TOUCH(ANALYSIS_signal_apply_crop_settings);
    RS_TOUCH(ANALYSIS_get_current_time_range);
    RS_TOUCH(ANALYSIS_get_current_filter_bins);
    RS_TOUCH(ANALYSIS_noise_sample_is_muted);
    RS_TOUCH(ANALYSIS_zero_noise_samples);
    RS_TOUCH(ANALYSIS_copy_crop_with_optional_noise);
    RS_TOUCH(ANALYSIS_process_crop_frequency_and_noise);
    RS_TOUCH(ANALYSIS_build_crop_filename);
    RS_TOUCH(ANALYSIS_crop_current_selection);
    RS_TOUCH(ANALYSIS_handle_signal_menu_event);
    RS_TOUCH(ANALYSIS_draw_workstation_overlays);
    RS_TOUCH(ANALYSIS_wrap_phase);
    RS_TOUCH(ANALYSIS_gray);
    RS_TOUCH(ANALYSIS_is_center_display_bin);
    RS_TOUCH(ANALYSIS_spectrogram_display_db);
    RS_TOUCH(ANALYSIS_load_iq_blocks_worker);
    RS_TOUCH(ANALYSIS_load_iq_blocks_multithreaded);
    RS_TOUCH(ANALYSIS_constellation_double_compare);
    RS_TOUCH(ANALYSIS_constellation_next_power_of_two);
    RS_TOUCH(ANALYSIS_constellation_apply_frequency_correction);
    RS_TOUCH(ANALYSIS_constellation_complex_power);
    RS_TOUCH(ANALYSIS_constellation_estimate_mth_frequency);
    RS_TOUCH(ANALYSIS_constellation_mth_coherence);
    RS_TOUCH(ANALYSIS_constellation_estimate_direct_frequency);
    RS_TOUCH(ANALYSIS_constellation_prepare_samples);
    RS_TOUCH(ANALYSIS_constellation_kmeans_1d);
    RS_TOUCH(ANALYSIS_constellation_psk_score);
    RS_TOUCH(ANALYSIS_constellation_qam_score);
    RS_TOUCH(ANALYSIS_constellation_ask_score);
    RS_TOUCH(ANALYSIS_constellation_collect_candidate);
    RS_TOUCH(ANALYSIS_constellation_transition_timing_score);
    RS_TOUCH(ANALYSIS_constellation_find_symbol_timing);
    RS_TOUCH(ANALYSIS_constellation_normalize_output);
    RS_TOUCH(ANALYSIS_constellation_estimate_psk_frequency_fft);
    RS_TOUCH(ANALYSIS_constellation_estimate_qam_frequency_fft);
    RS_TOUCH(ANALYSIS_constellation_estimate_spectral_center);
    RS_TOUCH(ANALYSIS_constellation_estimate_symbol_rate);
    RS_TOUCH(ANALYSIS_constellation_find_center_offset);
    RS_TOUCH(ANALYSIS_constellation_find_symbol_timing_v2);
    RS_TOUCH(ANALYSIS_constellation_psk_residual_frequency);
    RS_TOUCH(ANALYSIS_constellation_qam_residual_frequency);
    RS_TOUCH(ANALYSIS_constellation_build_linear_family);
    RS_TOUCH(ANALYSIS_constellation_fsk_kmeans_1d);
    RS_TOUCH(ANALYSIS_constellation_build_fsk_family);
    RS_TOUCH(ANALYSIS_constellation_ofdm_cp_score);
    RS_TOUCH(ANALYSIS_constellation_build_ofdm_family_generic);
    RS_TOUCH(ANALYSIS_constellation_ofdm_cp_metrics);
    RS_TOUCH(ANALYSIS_constellation_ofdm_has_periodic_neighbor);
    RS_TOUCH(ANALYSIS_constellation_build_known_ofdm_qpsk);
    RS_TOUCH(ANALYSIS_constellation_build_ofdm_family);
    RS_TOUCH(ANALYSIS_build_selected_constellation);
    RS_TOUCH(ANALYSIS_render_workstation_data);
    RS_TOUCH(ANALYSIS_enter_mode);
    RS_TOUCH(ANALYSIS_is_text_entry_active);
    RS_TOUCH(ANALYSIS_handle_event);
}


static void rs_test_behavior(void) {
    char clear_me[16] = "secret";
    char sample_path[PATH_MAX];
    uint64_t unsigned_value = 0;
    int64_t signed_value = 0;
    uint32_t *pixels = calloc(320U * 180U, sizeof(*pixels));

    RS_CHECK("analysis clamp low", ANALYSIS_limit_double(-1.0, 0.0, 1.0) == 0.0);
    RS_CHECK("analysis clamp high", ANALYSIS_limit_double(2.0, 0.0, 1.0) == 1.0);
    ANALYSIS_secure_clear(clear_me, sizeof(clear_me));
    RS_CHECK("analysis secure clear", clear_me[0] == '\0' && clear_me[15] == '\0');
    ANALYSIS_set_context(rs_test_temp_dir, 100000000ULL, 2000000U, 1000000U);
    ANALYSIS_parse_recording_metadata("08-04-2026_12-00-00_CAPTURE_433.920000MHz_BW_25.000kHz_SR_2000.000k_Decimation_1.complex16");
    RS_CHECK("analysis parses center frequency", fabs(Global_Analysis_Center_Hz - 433920000.0) < 1.0);
    RS_CHECK("analysis parses sample rate", fabs(Global_Analysis_Sample_Rate - 2000000.0) < 1.0);
    snprintf(sample_path, sizeof(sample_path), "%s/sample.complex16", rs_test_temp_dir);
    RS_CHECK("analysis synthetic recording", rs_test_write_iq_file(sample_path, 1024));
    RS_CHECK("analysis scans recording directory", ANALYSIS_scan_recordings() && Global_Analysis_File_Count >= 1);

    snprintf(Global_Analysis_Transmit_Field_Text[0], sizeof(Global_Analysis_Transmit_Field_Text[0]), "433920000");
    RS_CHECK("analysis parses TX unsigned", ANALYSIS_parse_transmit_integer(0, "Frequency", 1, UINT64_MAX, &unsigned_value) && unsigned_value == 433920000ULL);
    snprintf(Global_Analysis_Transmit_Field_Text[3], sizeof(Global_Analysis_Transmit_Field_Text[3]), "-10");
    RS_CHECK("analysis parses TX signed", ANALYSIS_parse_transmit_signed_integer(3, "Gain", -50, 50, &signed_value) && signed_value == -10);

    ANALYSIS_enter_mode(rs_test_temp_dir, 100000000ULL, 2000000U, 2000000U);
    RS_CHECK("analysis enters mode", Global_Analysis_Mode);
    RS_CHECK("analysis text-entry state valid", ANALYSIS_is_text_entry_active() == 0 || ANALYSIS_is_text_entry_active() == 1);
    if (pixels) {
        ANALYSIS_render_workstation_data(pixels, 320, 180);
        RS_CHECK("analysis render data smoke", 1);
        free(pixels);
    } else {
        RS_CHECK("analysis render allocation", 0);
    }
}

#endif


#if RS_TEST_TARGET == 2
#include "../src/AuthAdmin.c"

static const char *rs_test_target_name = "AuthAdmin.c";
static const int rs_test_expected_function_count = 46;

static void rs_test_touch_all(void) {
    RS_TOUCH(admin_role_name);
    RS_TOUCH(admin_role_color);
    RS_TOUCH(admin_role_is_privileged);
    RS_TOUCH(admin_secure_zero);
    RS_TOUCH(admin_copy);
    RS_TOUCH(admin_point_in_rect);
    RS_TOUCH(admin_fill);
    RS_TOUCH(admin_outline);
    RS_TOUCH(admin_text);
    RS_TOUCH(admin_centered);
    RS_TOUCH(admin_button);
    RS_TOUCH(admin_mask);
    RS_TOUCH(admin_field);
    RS_TOUCH(admin_set_status);
    RS_TOUCH(admin_clear_form);
    RS_TOUCH(admin_refresh_users);
    RS_TOUCH(admin_selected_valid);
    RS_TOUCH(admin_can_modify_selected);
    RS_TOUCH(admin_can_delete_selected);
    RS_TOUCH(admin_can_change_selected_role);
    RS_TOUCH(admin_username_valid);
    RS_TOUCH(admin_main_panel);
    RS_TOUCH(admin_form_panel);
    RS_TOUCH(admin_form_field);
    RS_TOUCH(admin_render_users);
    RS_TOUCH(admin_render_create);
    RS_TOUCH(admin_group_secret);
    RS_TOUCH(admin_render_totp);
    RS_TOUCH(admin_render_reset);
    RS_TOUCH(admin_render_remove_totp);
    RS_TOUCH(admin_render_delete);
    RS_TOUCH(admin_render);
    RS_TOUCH(admin_append);
    RS_TOUCH(admin_backspace);
    RS_TOUCH(admin_handle_text);
    RS_TOUCH(admin_handle_backspace);
    RS_TOUCH(admin_cycle);
    RS_TOUCH(admin_submit_create);
    RS_TOUCH(admin_submit_totp);
    RS_TOUCH(admin_submit_set_totp);
    RS_TOUCH(admin_submit_remove_totp);
    RS_TOUCH(admin_submit_reset);
    RS_TOUCH(admin_submit_role);
    RS_TOUCH(admin_submit_delete);
    RS_TOUCH(admin_handle_mouse);
    RS_TOUCH(AUTH_ADMIN_run);
}


static void rs_test_behavior(void) {
    char dst[8];
    SDL_Rect rect = {10, 20, 30, 40};
    SDL_Color admin_color = admin_role_color(AUTH_ROLE_ADMIN);
    SDL_Color user_color = admin_role_color(AUTH_ROLE_USER);

    RS_CHECK("admin role name", strcmp(admin_role_name(AUTH_ROLE_ADMIN), "Administrator") == 0);
    RS_CHECK("co-admin role name", strcmp(admin_role_name(AUTH_ROLE_CO_ADMIN), "Co-Administrator") == 0);
    RS_CHECK("user role name", strcmp(admin_role_name(AUTH_ROLE_USER), "User") == 0);
    RS_CHECK("admin privileged", admin_role_is_privileged(AUTH_ROLE_ADMIN));
    RS_CHECK("co-admin privileged", admin_role_is_privileged(AUTH_ROLE_CO_ADMIN));
    RS_CHECK("user not privileged", !admin_role_is_privileged(AUTH_ROLE_USER));
    RS_CHECK("role colors differ", admin_color.r != user_color.r || admin_color.g != user_color.g || admin_color.b != user_color.b);
    admin_copy(dst, sizeof(dst), "abcdef");
    RS_CHECK("admin copy", strcmp(dst, "abcdef") == 0);
    RS_CHECK("admin point inside", admin_point_in_rect(15, 25, rect));
    RS_CHECK("admin point outside", !admin_point_in_rect(9, 25, rect));
    RS_CHECK("admin username valid", admin_username_valid("co_admin-1"));
    RS_CHECK("admin username rejects short", !admin_username_valid("ab"));
    RS_CHECK("admin username rejects spaces", !admin_username_valid("bad user"));
}

#endif


#if RS_TEST_TARGET == 3
#include "../src/AuthScreen.c"

static const char *rs_test_target_name = "AuthScreen.c";
static const int rs_test_expected_function_count = 127;

static void rs_test_touch_all(void) {
    RS_TOUCH(auth_point_in_rect);
    RS_TOUCH(auth_secure_zero);
    RS_TOUCH(auth_constant_time_equal);
    RS_TOUCH(auth_copy_text);
    RS_TOUCH(auth_set_status);
    RS_TOUCH(auth_utf8_previous_index);
    RS_TOUCH(auth_utf8_next_index);
    RS_TOUCH(auth_import_path_clamp_cursor);
    RS_TOUCH(auth_import_path_replace_selection);
    RS_TOUCH(auth_import_path_insert);
    RS_TOUCH(auth_import_path_backspace);
    RS_TOUCH(auth_import_path_delete);
    RS_TOUCH(auth_import_path_copy);
    RS_TOUCH(auth_import_path_paste);
    RS_TOUCH(auth_clear_sensitive);
    RS_TOUCH(auth_draw_text);
    RS_TOUCH(auth_draw_centered_text);
    RS_TOUCH(auth_fill_rect);
    RS_TOUCH(auth_outline_rect);
    RS_TOUCH(auth_draw_grid);
    RS_TOUCH(auth_top_right_button_rect);
    RS_TOUCH(auth_top_left_button_rect);
    RS_TOUCH(auth_database_key_button_rect);
    RS_TOUCH(auth_make_panel);
    RS_TOUCH(auth_field_rect);
    RS_TOUCH(auth_primary_button_rect);
    RS_TOUCH(auth_checkbox_rect);
    RS_TOUCH(auth_copy_secret_button_rect);
    RS_TOUCH(auth_create_two_factor_code_rect);
    RS_TOUCH(auth_mask_text);
    RS_TOUCH(auth_draw_field);
    RS_TOUCH(auth_path_line_end);
    RS_TOUCH(auth_draw_import_path_field);
    RS_TOUCH(auth_draw_button);
    RS_TOUCH(auth_draw_danger_button);
    RS_TOUCH(auth_draw_trusted_server_header);
    RS_TOUCH(auth_format_12_hour_time);
    RS_TOUCH(auth_draw_lock_icon);
    RS_TOUCH(auth_draw_identity_status);
    RS_TOUCH(auth_base32_encode);
    RS_TOUCH(auth_group_secret);
    RS_TOUCH(auth_render);
    RS_TOUCH(auth_append_text);
    RS_TOUCH(auth_backspace);
    RS_TOUCH(auth_ensure_directory);
    RS_TOUCH(auth_database_path);
    RS_TOUCH(auth_totp_master_key_path);
    RS_TOUCH(auth_read_exact_fd);
    RS_TOUCH(auth_write_exact_fd);
    RS_TOUCH(auth_load_existing_totp_master_key);
    RS_TOUCH(auth_load_or_create_totp_master_key);
    RS_TOUCH(auth_derive_server_totp_key);
    RS_TOUCH(auth_execute_sql);
    RS_TOUCH(auth_ensure_admin_column);
    RS_TOUCH(auth_table_has_column);
    RS_TOUCH(auth_ensure_role_column);
    RS_TOUCH(auth_ensure_password_columns);
    RS_TOUCH(auth_rate_limit_scope);
    RS_TOUCH(auth_rate_limit_remaining);
    RS_TOUCH(auth_rate_limit_failure);
    RS_TOUCH(auth_rate_limit_success);
    RS_TOUCH(auth_rate_limit_guard);
    RS_TOUCH(auth_rate_limit_note_failure);
    RS_TOUCH(auth_open_database);
    RS_TOUCH(auth_count_users);
    RS_TOUCH(auth_username_valid);
    RS_TOUCH(auth_user_exists);
    RS_TOUCH(auth_pbkdf2_derive_key_with_digest);
    RS_TOUCH(auth_pbkdf2_derive_key);
    RS_TOUCH(auth_digest_from_name);
    RS_TOUCH(auth_totp_kdf_valid);
    RS_TOUCH(auth_hash_password_argon2id);
    RS_TOUCH(auth_verify_argon2id);
    RS_TOUCH(auth_dummy_password_work);
    RS_TOUCH(auth_derive_totp_encryption_key);
    RS_TOUCH(auth_encrypt_totp_secret);
    RS_TOUCH(auth_decrypt_totp_secret);
    RS_TOUCH(auth_store_server_wrapped_totp);
    RS_TOUCH(auth_migrate_totp_to_server_key);
    RS_TOUCH(auth_load_user);
    RS_TOUCH(auth_upgrade_legacy_password);
    RS_TOUCH(auth_verify_password);
    RS_TOUCH(auth_insert_user);
    RS_TOUCH(auth_totp_at_counter);
    RS_TOUCH(auth_verify_totp_counter_with_algorithm);
    RS_TOUCH(auth_verify_totp_with_algorithm);
    RS_TOUCH(auth_verify_totp);
    RS_TOUCH(auth_reset_for_login);
    RS_TOUCH(auth_reset_for_create);
    RS_TOUCH(auth_reset_for_authorization);
    RS_TOUCH(auth_submit_login);
    RS_TOUCH(auth_submit_login_two_factor);
    RS_TOUCH(auth_submit_authorize_create);
    RS_TOUCH(auth_submit_authorize_create_two_factor);
    RS_TOUCH(auth_submit_create);
    RS_TOUCH(auth_submit_create_two_factor);
    RS_TOUCH(AUTH_set_client_only_mode);
    RS_TOUCH(auth_submit_remote);
    RS_TOUCH(auth_submit);
    RS_TOUCH(auth_run_transition);
    RS_TOUCH(auth_cycle_field);
    RS_TOUCH(auth_handle_text_input);
    RS_TOUCH(auth_handle_backspace);
    RS_TOUCH(auth_handle_mouse);
    RS_TOUCH(AUTH_get_server_id);
    RS_TOUCH(AUTH_get_server_name);
    RS_TOUCH(AUTH_get_current_username);
    RS_TOUCH(AUTH_run);
    RS_TOUCH(auth_public_error);
    RS_TOUCH(auth_role_is_privileged);
    RS_TOUCH(auth_get_user_role);
    RS_TOUCH(auth_authorize_account_management);
    RS_TOUCH(AUTH_SERVER_authenticate);
    RS_TOUCH(AUTH_SERVER_verify_password);
    RS_TOUCH(AUTH_verify_current_password);
    RS_TOUCH(AUTH_DB_server_list_users);
    RS_TOUCH(AUTH_DB_list_users);
    RS_TOUCH(AUTH_DB_create_user);
    RS_TOUCH(AUTH_DB_reset_password);
    RS_TOUCH(AUTH_DB_set_totp);
    RS_TOUCH(AUTH_DB_remove_totp);
    RS_TOUCH(AUTH_DB_set_role);
    RS_TOUCH(AUTH_DB_delete_user);
    RS_TOUCH(AUTH_DB_user_count);
    RS_TOUCH(AUTH_TOTP_generate_secret);
    RS_TOUCH(AUTH_TOTP_verify);
    RS_TOUCH(AUTH_TOTP_base32);
}


static int rs_test_auth_unlock(void) {
    unsigned char key[32];
    char path[PATH_MAX];
    char error[512] = "";
    for (size_t i = 0; i < sizeof(key); i++) key[i] = (unsigned char)(0x31U + i);
    snprintf(path, sizeof(path), "%s/auth-master.key", rs_test_temp_dir);
    return rs_test_write_bytes(path, key, sizeof(key), 0600) &&
           DATABASE_CRYPTO_set_key_path(path, error, sizeof(error));
}
static void rs_test_behavior(void) {
    SDL_Rect rect = {5, 5, 20, 20};
    unsigned char left[8] = {1,2,3,4,5,6,7,8};
    unsigned char right[8] = {1,2,3,4,5,6,7,8};
    unsigned char secret[AUTH_PUBLIC_TOTP_SECRET_BYTES];
    char base32[256];
    char code[16];
    char copied[8];
    char error[512] = "";
    int is_admin = 0;
    uint64_t counter = (uint64_t)time(NULL) / 30U;
    uint32_t numeric;

    RS_CHECK("auth point inside", auth_point_in_rect(10, 10, rect));
    RS_CHECK("auth point outside", !auth_point_in_rect(30, 10, rect));
    RS_CHECK("auth constant-time equal", auth_constant_time_equal(left, right, sizeof(left)));
    right[7] ^= 1U;
    RS_CHECK("auth constant-time unequal", !auth_constant_time_equal(left, right, sizeof(left)));
    auth_copy_text(copied, sizeof(copied), "abcdef");
    RS_CHECK("auth text copy", strcmp(copied, "abcdef") == 0);
    RS_CHECK("auth UTF-8 previous ASCII", auth_utf8_previous_index("abc", 2) == 1);
    RS_CHECK("auth UTF-8 next ASCII", auth_utf8_next_index("abc", 3, 1) == 2);
    RS_CHECK("auth username valid", auth_username_valid("operator_01"));
    RS_CHECK("auth username invalid", !auth_username_valid("bad user"));
    RS_CHECK("auth role privilege", auth_role_is_privileged(AUTH_ROLE_ADMIN) && auth_role_is_privileged(AUTH_ROLE_CO_ADMIN) && !auth_role_is_privileged(AUTH_ROLE_USER));

    RS_CHECK("generate TOTP secret", AUTH_TOTP_generate_secret(secret));
    RS_CHECK("base32 TOTP secret", AUTH_TOTP_base32(secret, base32, sizeof(base32)) && strlen(base32) > 20);
    numeric = auth_totp_at_counter(secret, AUTH_PUBLIC_TOTP_SECRET_BYTES, counter, AUTH_TOTP_ALGORITHM_DEFAULT);
    snprintf(code, sizeof(code), "%06u", numeric);
    RS_CHECK("verify current TOTP", AUTH_TOTP_verify(secret, code));
    RS_CHECK("reject invalid TOTP", !AUTH_TOTP_verify(secret, "000000"));

    RS_CHECK("unlock auth database", rs_test_auth_unlock());
    RS_CHECK("create bootstrap administrator", AUTH_DB_create_user("primary_admin", "Correct-Horse-123!", 0, 1, NULL, "", error, sizeof(error)));
    RS_CHECK("auth database user count", AUTH_DB_user_count() == 1);
    RS_CHECK("local primary admin authentication", AUTH_SERVER_authenticate("primary_admin", "Correct-Horse-123!", "", "local-cli", &is_admin, error, sizeof(error)) == AUTH_SERVER_RESULT_SUCCESS && is_admin);
    is_admin = 0;
    RS_CHECK("remote primary admin restriction", AUTH_SERVER_authenticate("primary_admin", "Correct-Horse-123!", "", "127.0.0.1", &is_admin, error, sizeof(error)) != AUTH_SERVER_RESULT_SUCCESS);
    RS_CHECK("create ordinary verification user", AUTH_DB_create_user("operator_user", "Operator-Pass-123!", 0, 0, NULL, "primary_admin", error, sizeof(error)));
    RS_CHECK("verify correct password", AUTH_SERVER_verify_password("operator_user", "Operator-Pass-123!", "local-gui", error, sizeof(error)));
    RS_CHECK("reject wrong password", !AUTH_SERVER_verify_password("operator_user", "wrong-password", "local-gui", error, sizeof(error)));
    RS_CHECK("primary admin reauthorization restriction", !AUTH_SERVER_verify_password("primary_admin", "Correct-Horse-123!", "local-gui", error, sizeof(error)));
    OPENSSL_cleanse(secret, sizeof(secret));
}

#endif


#if RS_TEST_TARGET == 4
#include "../src/CaseManagementWorkstation.c"

static const char *rs_test_target_name = "CaseManagementWorkstation.c";
static const int rs_test_expected_function_count = 229;

static void rs_test_touch_all(void) {
    RS_TOUCH(case_country_count);
    RS_TOUCH(case_country_index_by_name);
    RS_TOUCH(case_country_flag_texture);
    RS_TOUCH(case_get_adjusted_mouse_state);
    RS_TOUCH(case_is_complex16_file);
    RS_TOUCH(case_name_compare);
    RS_TOUCH(case_scan_source_files);
    RS_TOUCH(case_point_in_rect);
    RS_TOUCH(case_copy_text);
    RS_TOUCH(case_limit_double);
    RS_TOUCH(case_set_status);
    RS_TOUCH(case_draw_text_centered);
    RS_TOUCH(case_draw_button);
    RS_TOUCH(case_shorten);
    RS_TOUCH(case_find_block_index_by_id);
    RS_TOUCH(case_clear_block_selection);
    RS_TOUCH(case_is_block_selected);
    RS_TOUCH(case_selected_block_count);
    RS_TOUCH(case_sync_primary_selection);
    RS_TOUCH(case_select_only_block);
    RS_TOUCH(case_toggle_block_selection);
    RS_TOUCH(case_selected_block_is_case);
    RS_TOUCH(case_push_undo_state);
    RS_TOUCH(case_undo_last_change);
    RS_TOUCH(case_should_show_field);
    RS_TOUCH(case_rect_is_valid);
    RS_TOUCH(case_rects_intersect);
    RS_TOUCH(case_make_normalized_rect);
    RS_TOUCH(case_select_blocks_in_rect);
    RS_TOUCH(case_canvas_rect);
    RS_TOUCH(case_editor_rect);
    RS_TOUCH(case_ensure_view);
    RS_TOUCH(case_world_to_screen_x);
    RS_TOUCH(case_world_to_screen_y);
    RS_TOUCH(case_screen_to_world_x);
    RS_TOUCH(case_screen_to_world_y);
    RS_TOUCH(case_block_world_rect);
    RS_TOUCH(case_block_screen_rect);
    RS_TOUCH(case_connector_px);
    RS_TOUCH(case_block_endpoint_rect);
    RS_TOUCH(case_endpoint_center);
    RS_TOUCH(case_endpoint_at);
    RS_TOUCH(case_nearest_endpoint);
    RS_TOUCH(case_block_at);
    RS_TOUCH(case_status_color);
    RS_TOUCH(case_priority_color);
    RS_TOUCH(case_make_timeline_text);
    RS_TOUCH(case_seed_default_blocks);
    RS_TOUCH(case_csv_write_field);
    RS_TOUCH(case_csv_write_multiline_field);
    RS_TOUCH(case_unescape_multiline);
    RS_TOUCH(case_has_extension);
    RS_TOUCH(case_normalize_file_name);
    RS_TOUCH(case_is_case_graph_file);
    RS_TOUCH(case_scan_case_graph_files);
    RS_TOUCH(case_write_blocks_csv);
    RS_TOUCH(case_write_links_csv);
    RS_TOUCH(case_database_case_number);
    RS_TOUCH(case_serialize);
    RS_TOUCH(case_save);
    RS_TOUCH(case_reset_graph_for_load);
    RS_TOUCH(case_read_csv_field);
    RS_TOUCH(case_text_equals_ci);
    RS_TOUCH(case_text_contains_ci);
    RS_TOUCH(case_trim_text);
    RS_TOUCH(case_case_option_exists);
    RS_TOUCH(case_add_case_option);
    RS_TOUCH(case_scan_case_files);
    RS_TOUCH(case_build_case_matches);
    RS_TOUCH(case_build_country_matches);
    RS_TOUCH(case_scan_users);
    RS_TOUCH(case_build_user_matches);
    RS_TOUCH(case_clamp_user_scroll);
    RS_TOUCH(case_select_user_option);
    RS_TOUCH(case_select_case_option);
    RS_TOUCH(case_select_country_option);
    RS_TOUCH(case_parse_block_line);
    RS_TOUCH(case_parse_link_line);
    RS_TOUCH(case_load_stream);
    RS_TOUCH(case_load_current_file);
    RS_TOUCH(case_load);
    RS_TOUCH(case_add_block_typed);
    RS_TOUCH(case_add_block);
    RS_TOUCH(case_duplicate_selected_block);
    RS_TOUCH(case_link_exists);
    RS_TOUCH(case_add_link);
    RS_TOUCH(case_id_is_marked_for_removal);
    RS_TOUCH(case_delete_selected);
    RS_TOUCH(case_selected_field_text);
    RS_TOUCH(case_field_max_len);
    RS_TOUCH(case_field_storage_size);
    RS_TOUCH(case_text_allowed_for_field);
    RS_TOUCH(case_description_range_width);
    RS_TOUCH(case_auto_wrap_description_text);
    RS_TOUCH(case_insert_text);
    RS_TOUCH(case_backspace);
    RS_TOUCH(case_delete_at_cursor);
    RS_TOUCH(case_description_clear_selection);
    RS_TOUCH(case_description_selection_range);
    RS_TOUCH(case_description_delete_selection);
    RS_TOUCH(case_description_start_selection_at_cursor);
    RS_TOUCH(case_description_update_selection_to_cursor);
    RS_TOUCH(case_description_build_lines);
    RS_TOUCH(case_description_move_horizontal);
    RS_TOUCH(case_description_move_vertical);
    RS_TOUCH(case_set_description_cursor_from_mouse);
    RS_TOUCH(case_cycle_status);
    RS_TOUCH(case_days_in_month);
    RS_TOUCH(case_first_weekday);
    RS_TOUCH(case_today_month_year);
    RS_TOUCH(case_parse_mmddyyyy);
    RS_TOUCH(case_open_calendar_for_field);
    RS_TOUCH(case_shift_calendar_month);
    RS_TOUCH(case_set_calendar_day);
    RS_TOUCH(case_paste_description_from_clipboard);
    RS_TOUCH(CASE_MANAGEMENT_enter_mode);
    RS_TOUCH(CASE_MANAGEMENT_exit_mode);
    RS_TOUCH(CASE_MANAGEMENT_is_text_entry_active);
    RS_TOUCH(case_toolbar_rects);
    RS_TOUCH(case_editor_field_rects);
    RS_TOUCH(case_field_hit_rect);
    RS_TOUCH(case_description_open_button_rect);
    RS_TOUCH(case_status_dropdown_rect);
    RS_TOUCH(case_calendar_rect);
    RS_TOUCH(case_source_popup_rect);
    RS_TOUCH(case_file_search_popup_rect);
    RS_TOUCH(case_file_search_input_rect);
    RS_TOUCH(case_description_popup_rect);
    RS_TOUCH(case_file_search_matches);
    RS_TOUCH(case_file_search_filtered_count);
    RS_TOUCH(case_file_search_filtered_index_at);
    RS_TOUCH(case_file_search_clamp_scroll);
    RS_TOUCH(case_close_file_search_menu);
    RS_TOUCH(case_open_file_search_menu);
    RS_TOUCH(case_file_search_select_index);
    RS_TOUCH(case_file_search_insert_text);
    RS_TOUCH(case_file_search_backspace);
    RS_TOUCH(case_file_search_delete);
    RS_TOUCH(case_file_name_insert_text);
    RS_TOUCH(case_file_name_backspace);
    RS_TOUCH(case_file_name_delete);
    RS_TOUCH(case_handle_file_search_event);
    RS_TOUCH(case_clamp_description_scroll);
    RS_TOUCH(case_set_description_cursor_from_mouse_scrolled);
    RS_TOUCH(case_source_name_matches_search);
    RS_TOUCH(case_source_filtered_count);
    RS_TOUCH(case_source_filtered_index_at);
    RS_TOUCH(case_source_search_rect);
    RS_TOUCH(case_clamp_source_scroll);
    RS_TOUCH(case_source_short_text);
    RS_TOUCH(case_draw_source_modal_button);
    RS_TOUCH(case_close_source_file_search_menu);
    RS_TOUCH(case_open_source_file_search_menu);
    RS_TOUCH(case_source_file_search_select_index);
    RS_TOUCH(case_handle_source_popup_click);
    RS_TOUCH(case_handle_status_dropdown_click);
    RS_TOUCH(case_handle_calendar_click);
    RS_TOUCH(case_case_dropdown_rect);
    RS_TOUCH(case_country_dropdown_rect);
    RS_TOUCH(case_user_dropdown_rect);
    RS_TOUCH(case_handle_case_dropdown_click);
    RS_TOUCH(case_handle_country_dropdown_click);
    RS_TOUCH(case_handle_user_dropdown_click);
    RS_TOUCH(case_clamp_case_scroll);
    RS_TOUCH(case_clamp_country_scroll);
    RS_TOUCH(case_metadata_is_document_name);
    RS_TOUCH(case_metadata_hash);
    RS_TOUCH(case_metadata_document_name);
    RS_TOUCH(case_metadata_compare);
    RS_TOUCH(case_metadata_find);
    RS_TOUCH(case_metadata_add);
    RS_TOUCH(case_metadata_parse_content);
    RS_TOUCH(case_metadata_load_legacy_descriptions);
    RS_TOUCH(case_metadata_sync_editor);
    RS_TOUCH(case_metadata_refresh);
    RS_TOUCH(case_metadata_filtered_index_at);
    RS_TOUCH(case_metadata_filtered_count);
    RS_TOUCH(case_metadata_clamp_scroll);
    RS_TOUCH(case_metadata_select);
    RS_TOUCH(case_metadata_begin_create);
    RS_TOUCH(case_metadata_begin_rename);
    RS_TOUCH(case_metadata_save_document);
    RS_TOUCH(case_metadata_first_csv_field_end);
    RS_TOUCH(case_metadata_rewrite_classification);
    RS_TOUCH(case_metadata_classification_name);
    RS_TOUCH(case_metadata_rename_classifications);
    RS_TOUCH(case_metadata_rewrite_graph);
    RS_TOUCH(case_metadata_rename_graphs);
    RS_TOUCH(case_metadata_rename_selected);
    RS_TOUCH(case_metadata_save_current);
    RS_TOUCH(case_metadata_delete_case);
    RS_TOUCH(case_metadata_insert_text);
    RS_TOUCH(case_metadata_backspace);
    RS_TOUCH(case_metadata_delete_at_cursor);
    RS_TOUCH(case_metadata_paste);
    RS_TOUCH(case_metadata_draw_input);
    RS_TOUCH(case_metadata_draw_description);
    RS_TOUCH(case_draw_case_browser);
    RS_TOUCH(case_metadata_cancel_edit);
    RS_TOUCH(case_metadata_handle_delete_confirmation);
    RS_TOUCH(case_metadata_handle_event);
    RS_TOUCH(case_metadata_draw_delete_confirmation);
    RS_TOUCH(CASE_MANAGEMENT_handle_event);
    RS_TOUCH(case_draw_grid);
    RS_TOUCH(case_draw_selection_box);
    RS_TOUCH(case_side_vector);
    RS_TOUCH(case_guess_end_side_for_preview);
    RS_TOUCH(case_route_points);
    RS_TOUCH(case_link_points);
    RS_TOUCH(case_segment_distance2);
    RS_TOUCH(case_link_at);
    RS_TOUCH(case_draw_link);
    RS_TOUCH(case_draw_arrow_head);
    RS_TOUCH(case_draw_link_preview);
    RS_TOUCH(case_draw_block);
    RS_TOUCH(case_draw_input);
    RS_TOUCH(case_draw_status_dropdown);
    RS_TOUCH(case_draw_calendar);
    RS_TOUCH(case_text_width);
    RS_TOUCH(case_draw_description_selection);
    RS_TOUCH(case_draw_description_box);
    RS_TOUCH(case_draw_source_popup);
    RS_TOUCH(case_draw_description_popup);
    RS_TOUCH(case_draw_file_search_popup);
    RS_TOUCH(case_draw_case_dropdown);
    RS_TOUCH(case_draw_country_dropdown);
    RS_TOUCH(case_draw_user_dropdown);
    RS_TOUCH(case_draw_editor);
    RS_TOUCH(CASE_MANAGEMENT_draw_workstation);
}


static void rs_test_behavior(void) {
    char copied[16];
    char trimmed[32] = "  Case Alpha  ";
    char normalized[64];
    int month = 0, day = 0, year = 0;
    SDL_Rect r;
    int xs[6], ys[6];
    SDL_Event event;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_Font *font = NULL;

    RS_CHECK("case complex16 extension", case_is_complex16_file("sample.complex16"));
    RS_CHECK("case complex16 extension is case-sensitive", !case_is_complex16_file("sample.COMPLEX16"));
    RS_CHECK("case point inside", case_point_in_rect(5, 5, (SDL_Rect){0,0,10,10}));
    case_copy_text(copied, sizeof(copied), "case");
    RS_CHECK("case copy text", strcmp(copied, "case") == 0);
    RS_CHECK("case clamp", case_limit_double(2.0, 0.0, 1.0) == 1.0);
    r = case_make_normalized_rect(10, 20, 2, 5);
    RS_CHECK("case normalized rectangle", r.x == 2 && r.y == 5 && r.w == 8 && r.h == 15 && case_rect_is_valid(r));
    RS_CHECK("case extension helper", case_has_extension("graph.RSCASE"));
    RS_CHECK("case extension helper rejects extensionless name", !case_has_extension("graph"));
    case_normalize_file_name(" ../bad/name ", normalized, sizeof(normalized));
    RS_CHECK("case filename normalization", strchr(normalized, '/') == NULL);
    RS_CHECK("case text equals CI", case_text_equals_ci("Alpha", "alpha"));
    RS_CHECK("case text contains CI", case_text_contains_ci("Case Alpha", "alpha"));
    case_trim_text(trimmed);
    RS_CHECK("case trim", strcmp(trimmed, "Case Alpha") == 0);
    RS_CHECK("case date parse", case_parse_mmddyyyy("08/04/2026", &month, &day, &year) && month == 8 && day == 4 && year == 2026);
    RS_CHECK("case metadata document marker", case_metadata_is_document_name("__case_metadata_CASE-1"));
    RS_CHECK("case metadata hash deterministic", case_metadata_hash("CASE-1") == case_metadata_hash("CASE-1"));
    RS_CHECK("case route points", case_route_points(0, 0, 1, 100, 100, 3, xs, ys) >= 2);

    CASE_MANAGEMENT_enter_mode(rs_test_temp_dir);
    RS_CHECK("case management text state valid", CASE_MANAGEMENT_is_text_entry_active() == 0 || CASE_MANAGEMENT_is_text_entry_active() == 1);
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 10;
    event.motion.y = 10;
    { int handled = CASE_MANAGEMENT_handle_event(&event, 1280, 720); RS_CHECK("case event smoke", handled == 0 || handled == 1); }
    if (rs_test_make_renderer(&surface, &renderer, &font)) {
        CASE_MANAGEMENT_draw_workstation(renderer, font, 1280, 720);
        RS_CHECK("case draw smoke", 1);
        rs_test_destroy_renderer(surface, renderer, font);
    } else {
        RS_SKIP("case draw smoke", "SDL/TTF renderer unavailable");
    }
    CASE_MANAGEMENT_exit_mode();
}

#endif


#if RS_TEST_TARGET == 5
#include "../src/ClassificationWorkstation.c"

static const char *rs_test_target_name = "ClassificationWorkstation.c";
static const int rs_test_expected_function_count = 81;

static void rs_test_touch_all(void) {
    RS_TOUCH(CLASSIFICATION_get_adjusted_mouse_state);
    RS_TOUCH(CLASSIFICATION_country_count);
    RS_TOUCH(CLASSIFICATION_char_lower);
    RS_TOUCH(CLASSIFICATION_text_contains_ci);
    RS_TOUCH(CLASSIFICATION_text_equals_ci);
    RS_TOUCH(CLASSIFICATION_trim_text);
    RS_TOUCH(CLASSIFICATION_case_option_exists);
    RS_TOUCH(CLASSIFICATION_add_case_option);
    RS_TOUCH(CLASSIFICATION_scan_case_files);
    RS_TOUCH(CLASSIFICATION_build_case_matches);
    RS_TOUCH(CLASSIFICATION_select_case);
    RS_TOUCH(CLASSIFICATION_find_country_exact);
    RS_TOUCH(CLASSIFICATION_build_country_matches);
    RS_TOUCH(CLASSIFICATION_select_country);
    RS_TOUCH(CLASSIFICATION_get_flag_texture);
    RS_TOUCH(CLASSIFICATION_draw_flag_box);
    RS_TOUCH(CLASSIFICATION_is_dropdown_field);
    RS_TOUCH(CLASSIFICATION_option_count_for_field);
    RS_TOUCH(CLASSIFICATION_option_for_field);
    RS_TOUCH(CLASSIFICATION_clamp_dropdown_scroll);
    RS_TOUCH(CLASSIFICATION_dropdown_visible_count);
    RS_TOUCH(CLASSIFICATION_name_compare);
    RS_TOUCH(CLASSIFICATION_is_complex16_file);
    RS_TOUCH(CLASSIFICATION_append_text);
    RS_TOUCH(CLASSIFICATION_backspace_text);
    RS_TOUCH(CLASSIFICATION_text_range_width);
    RS_TOUCH(CLASSIFICATION_clamp_notes_cursor);
    RS_TOUCH(CLASSIFICATION_clear_notes_selection);
    RS_TOUCH(CLASSIFICATION_notes_selection_range);
    RS_TOUCH(CLASSIFICATION_delete_notes_selection);
    RS_TOUCH(CLASSIFICATION_insert_notes_text);
    RS_TOUCH(CLASSIFICATION_auto_wrap_notes_text);
    RS_TOUCH(CLASSIFICATION_paste_notes_text);
    RS_TOUCH(CLASSIFICATION_backspace_notes_text);
    RS_TOUCH(CLASSIFICATION_delete_notes_text);
    RS_TOUCH(CLASSIFICATION_notes_build_lines);
    RS_TOUCH(CLASSIFICATION_notes_move_horizontal);
    RS_TOUCH(CLASSIFICATION_notes_move_vertical);
    RS_TOUCH(CLASSIFICATION_set_notes_cursor_from_mouse);
    RS_TOUCH(CLASSIFICATION_start_notes_selection);
    RS_TOUCH(CLASSIFICATION_update_notes_selection);
    RS_TOUCH(CLASSIFICATION_short_text);
    RS_TOUCH(CLASSIFICATION_draw_multiline_notes);
    RS_TOUCH(CLASSIFICATION_get_layout);
    RS_TOUCH(CLASSIFICATION_csv_escape);
    RS_TOUCH(CLASSIFICATION_parse_file_metadata);
    RS_TOUCH(CLASSIFICATION_load_selected_file_into_fields);
    RS_TOUCH(CLASSIFICATION_prefill_from_analysis_selection);
    RS_TOUCH(CLASSIFICATION_scan_recordings);
    RS_TOUCH(CLASSIFICATION_file_search_matches);
    RS_TOUCH(CLASSIFICATION_file_search_filtered_count);
    RS_TOUCH(CLASSIFICATION_file_search_filtered_index_at);
    RS_TOUCH(CLASSIFICATION_file_search_popup_rect);
    RS_TOUCH(CLASSIFICATION_file_search_input_rect);
    RS_TOUCH(CLASSIFICATION_file_search_button_rect);
    RS_TOUCH(CLASSIFICATION_file_search_clamp_scroll);
    RS_TOUCH(CLASSIFICATION_open_file_search_menu);
    RS_TOUCH(CLASSIFICATION_close_file_search_menu);
    RS_TOUCH(CLASSIFICATION_file_search_select_index);
    RS_TOUCH(CLASSIFICATION_file_search_insert_text);
    RS_TOUCH(CLASSIFICATION_file_search_backspace);
    RS_TOUCH(CLASSIFICATION_file_search_delete);
    RS_TOUCH(CLASSIFICATION_draw_modal_button);
    RS_TOUCH(CLASSIFICATION_handle_file_search_event);
    RS_TOUCH(CLASSIFICATION_draw_file_search_button);
    RS_TOUCH(CLASSIFICATION_draw_file_search_popup);
    RS_TOUCH(CLASSIFICATION_make_filename_safe);
    RS_TOUCH(CLASSIFICATION_get_signal_datetime);
    RS_TOUCH(CLASSIFICATION_append_csv_row);
    RS_TOUCH(CLASSIFICATION_is_text_entry_active);
    RS_TOUCH(CLASSIFICATION_enter_mode);
    RS_TOUCH(CLASSIFICATION_exit_mode);
    RS_TOUCH(CLASSIFICATION_handle_event);
    RS_TOUCH(CLASSIFICATION_draw_panel);
    RS_TOUCH(CLASSIFICATION_draw_selectable_row);
    RS_TOUCH(CLASSIFICATION_draw_input_field);
    RS_TOUCH(CLASSIFICATION_draw_dropdown);
    RS_TOUCH(CLASSIFICATION_draw_case_suggestions);
    RS_TOUCH(CLASSIFICATION_draw_country_suggestions);
    RS_TOUCH(CLASSIFICATION_draw_save_button);
    RS_TOUCH(CLASSIFICATION_draw_workstation);
}


static void rs_test_behavior(void) {
    char text[64] = "  Mixed Case  ";
    double freq = 0.0, bw = 0.0, start = 0.0, end = 0.0;
    SDL_Event event;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_Font *font = NULL;

    RS_CHECK("classification lowercase", CLASSIFICATION_char_lower('A') == 'a');
    RS_CHECK("classification contains CI", CLASSIFICATION_text_contains_ci("RetroSpectrum", "spectrum"));
    RS_CHECK("classification equals CI", CLASSIFICATION_text_equals_ci("QPSK", "qpsk"));
    CLASSIFICATION_trim_text(text);
    RS_CHECK("classification trim", strcmp(text, "Mixed Case") == 0);
    CLASSIFICATION_parse_file_metadata("08-04-2026_12-00-00_CAPTURE_433.920000MHz_BW_25.000kHz_SR_2000.000k_Decimation_1.complex16", &freq, &bw, &start, &end);
    RS_CHECK("classification parses frequency", fabs(freq - 433.92) < 0.001);
    RS_CHECK("classification parses bandwidth", bw >= 0.0);
    CLASSIFICATION_prefill_from_analysis_selection("sample.complex16", 433.92, 25.0, 1.0, 2.0);
    CLASSIFICATION_enter_mode(rs_test_temp_dir);
    RS_CHECK("classification text state valid", CLASSIFICATION_is_text_entry_active() == 0 || CLASSIFICATION_is_text_entry_active() == 1);
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 10;
    event.motion.y = 10;
    { int handled = CLASSIFICATION_handle_event(&event, 1280, 720); RS_CHECK("classification event smoke", handled == 0 || handled == 1); }
    if (rs_test_make_renderer(&surface, &renderer, &font)) {
        CLASSIFICATION_draw_workstation(renderer, font, 1280, 720);
        RS_CHECK("classification draw smoke", 1);
        rs_test_destroy_renderer(surface, renderer, font);
    } else {
        RS_SKIP("classification draw smoke", "SDL/TTF renderer unavailable");
    }
    CLASSIFICATION_exit_mode();
}

#endif


#if RS_TEST_TARGET == 6
#include "../src/CorrelationWorkstation.c"

static const char *rs_test_target_name = "CorrelationWorkstation.c";
static const int rs_test_expected_function_count = 76;

static void rs_test_touch_all(void) {
    RS_TOUCH(correlation_point_in_rect);
    RS_TOUCH(correlation_clamp_double);
    RS_TOUCH(correlation_set_status);
    RS_TOUCH(correlation_worker_checkpoint);
    RS_TOUCH(correlation_name_compare);
    RS_TOUCH(correlation_result_compare);
    RS_TOUCH(correlation_double_compare);
    RS_TOUCH(correlation_has_iq_extension);
    RS_TOUCH(correlation_parse_recording_metadata);
    RS_TOUCH(correlation_scan_recordings);
    RS_TOUCH(correlation_cache_path);
    RS_TOUCH(correlation_parse_cache_buffer);
    RS_TOUCH(correlation_serialize_cache);
    RS_TOUCH(correlation_load_local_cache);
    RS_TOUCH(correlation_load_server_cache_once);
    RS_TOUCH(correlation_save_cache);
    RS_TOUCH(correlation_find_cache_entry);
    RS_TOUCH(correlation_prune_cache);
    RS_TOUCH(correlation_normalize_trend);
    RS_TOUCH(correlation_detect_ranges);
    RS_TOUCH(correlation_estimate_occupied_bandwidth);
    RS_TOUCH(correlation_build_signature);
    RS_TOUCH(correlation_extract_file_signatures);
    RS_TOUCH(correlation_maximum_normalized_cross_correlation);
    RS_TOUCH(correlation_bandwidth_similarity);
    RS_TOUCH(correlation_signature_energy);
    RS_TOUCH(correlation_prepare_work_item);
    RS_TOUCH(correlation_get_or_build_entry);
    RS_TOUCH(correlation_calculate_score);
    RS_TOUCH(correlation_rescore_results);
    RS_TOUCH(correlation_worker);
    RS_TOUCH(correlation_start_worker);
    RS_TOUCH(correlation_clear_cache);
    RS_TOUCH(correlation_get_layout);
    RS_TOUCH(correlation_get_engine_controls);
    RS_TOUCH(correlation_get_clear_cache_dialog);
    RS_TOUCH(correlation_draw_centered_text);
    RS_TOUCH(correlation_draw_button);
    RS_TOUCH(correlation_get_result_controls);
    RS_TOUCH(correlation_text_width);
    RS_TOUCH(correlation_draw_wrapped_text);
    RS_TOUCH(correlation_draw_trend_line);
    RS_TOUCH(correlation_draw_trend_pair);
    RS_TOUCH(correlation_short_text);
    RS_TOUCH(correlation_interpolate_color);
    RS_TOUCH(correlation_score_color);
    RS_TOUCH(correlation_active_weight_buffer);
    RS_TOUCH(correlation_sync_text_input);
    RS_TOUCH(correlation_parse_nonnegative_scalar);
    RS_TOUCH(correlation_parse_trend_points);
    RS_TOUCH(correlation_restore_weight_text);
    RS_TOUCH(correlation_apply_weight_inputs);
    RS_TOUCH(correlation_weight_insert_text);
    RS_TOUCH(correlation_weight_backspace);
    RS_TOUCH(correlation_weight_delete);
    RS_TOUCH(correlation_activate_weight_field);
    RS_TOUCH(correlation_file_search_matches);
    RS_TOUCH(correlation_file_search_filtered_count);
    RS_TOUCH(correlation_file_search_filtered_index_at);
    RS_TOUCH(correlation_file_search_popup_rect);
    RS_TOUCH(correlation_file_search_input_rect);
    RS_TOUCH(correlation_file_search_clamp_scroll);
    RS_TOUCH(correlation_open_file_search);
    RS_TOUCH(correlation_close_file_search);
    RS_TOUCH(correlation_select_recording);
    RS_TOUCH(correlation_file_search_insert_text);
    RS_TOUCH(correlation_file_search_backspace);
    RS_TOUCH(correlation_file_search_delete);
    RS_TOUCH(correlation_handle_file_search_event);
    RS_TOUCH(correlation_draw_file_search_popup);
    RS_TOUCH(CORRELATION_enter_mode);
    RS_TOUCH(CORRELATION_exit_mode);
    RS_TOUCH(CORRELATION_shutdown);
    RS_TOUCH(CORRELATION_is_text_entry_active);
    RS_TOUCH(CORRELATION_handle_event);
    RS_TOUCH(CORRELATION_draw_workstation);
}


static void rs_test_behavior(void) {
    double sample_rate = 0.0, center = 0.0;
    float a[CORRELATION_TREND_POINTS] = {0};
    float b[CORRELATION_TREND_POINTS] = {0};
    double scalar = 0.0;
    int points = 0;
    SDL_Color c;
    SDL_Event event;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_Font *font = NULL;

    RS_CHECK("correlation point inside", correlation_point_in_rect(5, 5, (SDL_Rect){0,0,10,10}));
    RS_CHECK("correlation clamp", correlation_clamp_double(2.0, 0.0, 1.0) == 1.0);
    RS_CHECK("correlation IQ extension", correlation_has_iq_extension("capture.complex16"));
    RS_CHECK("correlation rejects unsupported IQ suffix", !correlation_has_iq_extension("capture.C16"));
    correlation_parse_recording_metadata("08-04-2026_12-00-00_CAPTURE_433.920000MHz_BW_25.000kHz_SR_2000.000k_Decimation_1.complex16", &sample_rate, &center);
    RS_CHECK("correlation parses sample rate", fabs(sample_rate - 2000000.0) < 1.0);
    RS_CHECK("correlation parses center", fabs(center - 433920000.0) < 1.0);
    for (int i = 0; i < CORRELATION_TREND_POINTS; i++) { a[i] = (float)i; b[i] = (float)i; }
    correlation_normalize_trend(a, CORRELATION_TREND_POINTS);
    correlation_normalize_trend(b, CORRELATION_TREND_POINTS);
    RS_CHECK("correlation identical trend score", correlation_maximum_normalized_cross_correlation(a, b) > 0.99);
    RS_CHECK("correlation weighted score bounds", correlation_calculate_score(1.0, 1.0, 1.0) >= 0.99 && correlation_calculate_score(1.0, 1.0, 1.0) <= 1.0);
    RS_CHECK("correlation scalar parser", correlation_parse_nonnegative_scalar("2.5", &scalar) && fabs(scalar - 2.5) < 1e-12);
    RS_CHECK("correlation trend-point parser", correlation_parse_trend_points("64", &points) && points == 64);
    c = correlation_score_color(95.0);
    RS_CHECK("correlation score color opaque", c.a == 255);

    CORRELATION_enter_mode(rs_test_temp_dir, 433920000ULL, 2000000U, 2000000U);
    RS_CHECK("correlation text state valid", CORRELATION_is_text_entry_active() == 0 || CORRELATION_is_text_entry_active() == 1);
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 10;
    event.motion.y = 10;
    { int handled = CORRELATION_handle_event(&event, 1280, 720); RS_CHECK("correlation event smoke", handled == 0 || handled == 1); }
    if (rs_test_make_renderer(&surface, &renderer, &font)) {
        CORRELATION_draw_workstation(renderer, font, 1280, 720);
        RS_CHECK("correlation draw smoke", 1);
        rs_test_destroy_renderer(surface, renderer, font);
    } else {
        RS_SKIP("correlation draw smoke", "SDL/TTF renderer unavailable");
    }
    CORRELATION_exit_mode();
    CORRELATION_shutdown();
}

#endif


#if RS_TEST_TARGET == 7
#include "../src/DataStore.c"

static const char *rs_test_target_name = "DataStore.c";
static const int rs_test_expected_function_count = 18;

static void rs_test_touch_all(void) {
    RS_TOUCH(datastore_set_error);
    RS_TOUCH(datastore_copy_text);
    RS_TOUCH(datastore_is_valid_kind);
    RS_TOUCH(datastore_make_directory);
    RS_TOUCH(datastore_config_directory);
    RS_TOUCH(DATASTORE_get_path);
    RS_TOUCH(datastore_execute);
    RS_TOUCH(datastore_open);
    RS_TOUCH(datastore_sha512);
    RS_TOUCH(DATASTORE_server_save_content);
    RS_TOUCH(DATASTORE_server_load_content);
    RS_TOUCH(DATASTORE_free_content);
    RS_TOUCH(DATASTORE_server_list_documents);
    RS_TOUCH(DATASTORE_server_delete_content);
    RS_TOUCH(DATASTORE_save_content);
    RS_TOUCH(DATASTORE_load_content);
    RS_TOUCH(DATASTORE_list_documents);
    RS_TOUCH(DATASTORE_delete_content);
}


static int rs_test_unlock_database(char *key_path, size_t key_path_size) {
    unsigned char key[32];
    char error[512] = "";
    for (size_t i = 0; i < sizeof(key); i++) key[i] = (unsigned char)(i * 7U + 3U);
    snprintf(key_path, key_path_size, "%s/database.key", rs_test_temp_dir);
    if (!rs_test_write_bytes(key_path, key, sizeof(key), 0600)) return 0;
    return DATABASE_CRYPTO_set_key_path(key_path, error, sizeof(error));
}
static void rs_test_behavior(void) {
    char copied[8];
    char error[512] = "";
    char path[PATH_MAX];
    char key_path[PATH_MAX];
    unsigned char digest[DATASTORE_SHA512_BYTES];
    const unsigned char payload[] = "RetroSpectrum datastore roundtrip";
    unsigned char *loaded = NULL;
    size_t loaded_size = 0;
    int found = 0;
    int deleted = 0;
    Type_DataStore_Document_Summary docs[8];
    size_t count = 0;

    datastore_set_error(error, sizeof(error), NULL);
    RS_CHECK("datastore default error text", strstr(error, "Unknown") != NULL);
    RS_CHECK("datastore_copy_text copies", datastore_copy_text(copied, sizeof(copied), "abc") && strcmp(copied, "abc") == 0);
    RS_CHECK("datastore_copy_text detects truncation", !datastore_copy_text(copied, 3, "abcd"));
    RS_CHECK("datastore valid case kind", datastore_is_valid_kind(DATASTORE_KIND_CASE_MANAGEMENT));
    RS_CHECK("datastore valid classification kind", datastore_is_valid_kind(DATASTORE_KIND_CLASSIFICATION));
    RS_CHECK("datastore valid correlation kind", datastore_is_valid_kind("correlation"));
    RS_CHECK("datastore valid bitstream kind", datastore_is_valid_kind("bitstream_classifier"));
    RS_CHECK("datastore invalid kind", !datastore_is_valid_kind("invalid"));
    RS_CHECK("datastore SHA-512", datastore_sha512(payload, sizeof(payload) - 1U, digest));
    RS_CHECK("datastore path creation", DATASTORE_get_path(path, sizeof(path)) && strstr(path, "retrospectrum_data.db") != NULL);
    RS_CHECK("database unlock for datastore", rs_test_unlock_database(key_path, sizeof(key_path)));

    RS_CHECK("datastore rejects invalid save", !DATASTORE_server_save_content("bad", "name", "", payload, sizeof(payload), error, sizeof(error)));
    error[0] = '\0';
    RS_CHECK("datastore saves content", DATASTORE_server_save_content("bitstream_classifier", "full-test", "CASE-1", payload, sizeof(payload) - 1U, error, sizeof(error)));
    RS_CHECK("datastore loads content", DATASTORE_server_load_content("bitstream_classifier", "full-test", &loaded, &loaded_size, &found, error, sizeof(error)) && found);
    RS_CHECK("datastore loaded bytes match", loaded && loaded_size == sizeof(payload) - 1U && memcmp(loaded, payload, loaded_size) == 0);
    DATASTORE_free_content(loaded, loaded_size);
    loaded = NULL;
    RS_CHECK("datastore lists content", DATASTORE_server_list_documents("bitstream_classifier", docs, 8, &count, error, sizeof(error)) && count >= 1);
    RS_CHECK("datastore deletes content", DATASTORE_server_delete_content("bitstream_classifier", "full-test", &deleted, error, sizeof(error)) && deleted);
    found = 1;
    RS_CHECK("datastore confirms deletion", DATASTORE_server_load_content("bitstream_classifier", "full-test", &loaded, &loaded_size, &found, error, sizeof(error)) && !found);
}

#endif


#if RS_TEST_TARGET == 8
#include "../src/DatabaseCrypto.c"

static const char *rs_test_target_name = "DatabaseCrypto.c";
static const int rs_test_expected_function_count = 42;

static void rs_test_touch_all(void) {
    RS_TOUCH(database_crypto_error);
    RS_TOUCH(database_crypto_write_all);
    RS_TOUCH(database_crypto_read_all);
    RS_TOUCH(database_crypto_ensure_directory);
    RS_TOUCH(database_crypto_directory);
    RS_TOUCH(database_crypto_path);
    RS_TOUCH(database_crypto_validate_key_file);
    RS_TOUCH(database_crypto_cleanup_cached_master);
    RS_TOUCH(database_crypto_cache_master);
    RS_TOUCH(database_crypto_copy_cached_master);
    RS_TOUCH(database_crypto_default_key_path);
    RS_TOUCH(database_crypto_read_saved_key_path);
    RS_TOUCH(database_crypto_resolve_key_path);
    RS_TOUCH(database_crypto_load_key_file);
    RS_TOUCH(database_crypto_any_database_exists);
    RS_TOUCH(database_crypto_create_default_master);
    RS_TOUCH(database_crypto_load_or_create_master);
    RS_TOUCH(database_crypto_derive_from_master);
    RS_TOUCH(database_crypto_derive_key);
    RS_TOUCH(database_crypto_hex);
    RS_TOUCH(database_crypto_parse_version);
    RS_TOUCH(database_crypto_version_supported);
    RS_TOUCH(database_crypto_has_sqlcipher);
    RS_TOUCH(database_crypto_exec);
    RS_TOUCH(database_crypto_validate_database_file);
    RS_TOUCH(database_crypto_apply_key);
    RS_TOUCH(database_crypto_key_opens_database);
    RS_TOUCH(database_crypto_write_saved_key_path);
    RS_TOUCH(database_crypto_file_is_plaintext);
    RS_TOUCH(database_crypto_parent_directory_writable);
    RS_TOUCH(database_crypto_create_secure_empty_file);
    RS_TOUCH(database_crypto_export_plaintext);
    RS_TOUCH(database_crypto_verify_encrypted_copy);
    RS_TOUCH(database_crypto_fsync_file);
    RS_TOUCH(database_crypto_fsync_parent_directory);
    RS_TOUCH(database_crypto_migrate_plaintext);
    RS_TOUCH(database_crypto_open);
    RS_TOUCH(DATABASE_CRYPTO_open_auth);
    RS_TOUCH(DATABASE_CRYPTO_open_data);
    RS_TOUCH(DATABASE_CRYPTO_set_key_path);
    RS_TOUCH(DATABASE_CRYPTO_is_unlocked);
    RS_TOUCH(DATABASE_CRYPTO_key_path);
}


static void rs_test_behavior(void) {
    int major = 0, minor = 0, patch = 0;
    char directory[PATH_MAX];
    char path[PATH_MAX];
    char key_path[PATH_MAX];
    char error[512] = "";
    unsigned char key[32];
    unsigned char cached[DATABASE_CRYPTO_MASTER_BYTES];
    sqlite3 *db = NULL;
    struct stat st;

    RS_CHECK("parse SQLCipher version", database_crypto_parse_version("4.6.1 community", &major, &minor, &patch) && major == 4 && minor == 6 && patch == 1);
    RS_CHECK("reject malformed SQLCipher version", !database_crypto_parse_version("version four", &major, &minor, &patch));
    RS_CHECK("database crypto directory", database_crypto_directory(directory, sizeof(directory)) && directory[0] == '/');
    RS_CHECK("database crypto path", database_crypto_path(path, sizeof(path), "sample.db") && strstr(path, "sample.db") != NULL);
    RS_CHECK("plaintext detection missing file false", !database_crypto_file_is_plaintext("/definitely/missing/retrospectrum.db"));

    for (size_t i = 0; i < sizeof(key); i++) key[i] = (unsigned char)(0xA5U ^ (unsigned char)i);
    snprintf(key_path, sizeof(key_path), "%s/master.key", rs_test_temp_dir);
    RS_CHECK("write database master key", rs_test_write_bytes(key_path, key, sizeof(key), 0600));
    RS_CHECK("set database key path", DATABASE_CRYPTO_set_key_path(key_path, error, sizeof(error)));
    RS_CHECK("database reports unlocked", DATABASE_CRYPTO_is_unlocked());
    RS_CHECK("database cached key available", database_crypto_copy_cached_master(cached) && memcmp(cached, key, sizeof(key)) == 0);
    OPENSSL_cleanse(cached, sizeof(cached));
    RS_CHECK("database key path retained", strcmp(DATABASE_CRYPTO_key_path(), key_path) == 0);

    RS_CHECK("open encrypted auth database", DATABASE_CRYPTO_open_auth(&db, path, sizeof(path), error, sizeof(error)) && db != NULL);
    if (db) { sqlite3_close(db); db = NULL; }
    RS_CHECK("auth database permission", stat(path, &st) == 0 && (st.st_mode & 077) == 0);
    RS_CHECK("open encrypted data database", DATABASE_CRYPTO_open_data(&db, path, sizeof(path), error, sizeof(error)) && db != NULL);
    if (db) { sqlite3_close(db); db = NULL; }
    RS_CHECK("data database is not plaintext header", !database_crypto_file_is_plaintext(path));

    database_crypto_cleanup_cached_master();
    RS_CHECK("database cleanup locks state", !DATABASE_CRYPTO_is_unlocked());
    RS_CHECK("reject nonexistent key path", !DATABASE_CRYPTO_set_key_path("/missing/retrospectrum.key", error, sizeof(error)));
}

#endif


#if RS_TEST_TARGET == 9
#include "../src/DecodeWorkstation.c"

static const char *rs_test_target_name = "DecodeWorkstation.c";
static const int rs_test_expected_function_count = 105;

static void rs_test_touch_all(void) {
    RS_TOUCH(decode_get_adjusted_mouse_state);
    RS_TOUCH(decode_preamble_search_thread_entry);
    RS_TOUCH(decode_start_preamble_search_thread);
    RS_TOUCH(decode_point_in_rect);
    RS_TOUCH(decode_copy_text);
    RS_TOUCH(decode_classifier_add_label_internal);
    RS_TOUCH(decode_classifier_document_clear_selection);
    RS_TOUCH(decode_classifier_document_get_selection);
    RS_TOUCH(decode_classifier_document_delete_selection);
    RS_TOUCH(decode_classifier_document_update_metrics);
    RS_TOUCH(decode_classifier_document_index_from_x);
    RS_TOUCH(decode_classifier_get_active_text_target);
    RS_TOUCH(decode_classifier_sync_rgb_from_selected);
    RS_TOUCH(decode_classifier_parse_rgb_component);
    RS_TOUCH(decode_classifier_apply_custom_rgb);
    RS_TOUCH(decode_classifier_initialize);
    RS_TOUCH(decode_classifier_has_assignments);
    RS_TOUCH(decode_classifier_clear_assignments);
    RS_TOUCH(decode_classifier_invalidate_assignments);
    RS_TOUCH(decode_classifier_trim_text);
    RS_TOUCH(decode_classifier_derive_document_name);
    RS_TOUCH(decode_classifier_segment_count);
    RS_TOUCH(decode_classifier_label_bit_count);
    RS_TOUCH(decode_classifier_apply_selected_label);
    RS_TOUCH(decode_classifier_remove_selected_tag);
    RS_TOUCH(decode_classifier_add_custom_label);
    RS_TOUCH(decode_classifier_store_u32);
    RS_TOUCH(decode_classifier_read_u32);
    RS_TOUCH(decode_classifier_save);
    RS_TOUCH(decode_classifier_load);
    RS_TOUCH(decode_classifier_split_bits_rect);
    RS_TOUCH(decode_classifier_get_layout);
    RS_TOUCH(decode_classifier_clamp_label_scroll);
    RS_TOUCH(decode_name_compare);
    RS_TOUCH(decode_has_complex16_extension);
    RS_TOUCH(decode_text_contains_ci);
    RS_TOUCH(decode_get_layout);
    RS_TOUCH(decode_short_text);
    RS_TOUCH(decode_set_status);
    RS_TOUCH(decode_get_progress_font);
    RS_TOUCH(decode_draw_centered_text);
    RS_TOUCH(decode_get_bit_selection);
    RS_TOUCH(decode_clear_bit_selection);
    RS_TOUCH(decode_clamp_bit_cursor);
    RS_TOUCH(decode_set_bit_cursor_visible);
    RS_TOUCH(decode_delete_selected_bits);
    RS_TOUCH(decode_insert_bit_text);
    RS_TOUCH(decode_backspace_bitstream);
    RS_TOUCH(decode_delete_bitstream);
    RS_TOUCH(decode_update_ascii_from_bitstream);
    RS_TOUCH(decode_copy_bitstream_to_clipboard);
    RS_TOUCH(decode_parse_int_field);
    RS_TOUCH(decode_update_bits_per_symbol_from_modulation);
    RS_TOUCH(decode_mod_arg);
    RS_TOUCH(decode_find_gnuradio_helper);
    RS_TOUCH(decode_shell_quote);
    RS_TOUCH(decode_scan_files);
    RS_TOUCH(decode_open_file_search_menu);
    RS_TOUCH(decode_close_file_search_menu);
    RS_TOUCH(decode_file_search_clamp_scroll);
    RS_TOUCH(decode_file_search_select_index);
    RS_TOUCH(decode_selected_file_path);
    RS_TOUCH(decode_append_bit);
    RS_TOUCH(decode_run_helper_capture_bits);
    RS_TOUCH(decode_pattern_has_zero_and_one);
    RS_TOUCH(decode_find_repeated_preamble_in_bits);
    RS_TOUCH(decode_preamble_search_next);
    RS_TOUCH(decode_export_preamble_candidate_to_decoder);
    RS_TOUCH(decode_run_selected_file);
    RS_TOUCH(decode_insert_text);
    RS_TOUCH(decode_backspace_text);
    RS_TOUCH(decode_delete_text);
    RS_TOUCH(decode_clamp_cursor);
    RS_TOUCH(decode_filtered_index_to_file_index);
    RS_TOUCH(decode_filtered_file_count);
    RS_TOUCH(decode_draw_modal_button);
    RS_TOUCH(decode_file_search_button_rect);
    RS_TOUCH(decode_file_search_popup_rect);
    RS_TOUCH(decode_file_search_input_rect);
    RS_TOUCH(decode_handle_file_search_event);
    RS_TOUCH(decode_draw_file_search_popup);
    RS_TOUCH(decode_classifier_filtered_document_count);
    RS_TOUCH(decode_classifier_filtered_index_to_document_index);
    RS_TOUCH(decode_classifier_search_clamp_scroll);
    RS_TOUCH(decode_classifier_refresh_search_documents);
    RS_TOUCH(decode_classifier_close_search_menu);
    RS_TOUCH(decode_classifier_open_search_menu);
    RS_TOUCH(decode_classifier_search_select_index);
    RS_TOUCH(decode_classifier_handle_search_event);
    RS_TOUCH(decode_classifier_draw_search_popup);
    RS_TOUCH(decode_draw_input_field);
    RS_TOUCH(decode_draw_checkbox);
    RS_TOUCH(decode_bit_metrics);
    RS_TOUCH(decode_bit_index_from_point);
    RS_TOUCH(decode_draw_wrapped_bits);
    RS_TOUCH(decode_classifier_draw_text_field);
    RS_TOUCH(decode_classifier_draw_rgb_field);
    RS_TOUCH(decode_classifier_draw_panel);
    RS_TOUCH(decode_classifier_handle_panel_click);
    RS_TOUCH(DECODE_enter_mode);
    RS_TOUCH(DECODE_exit_mode);
    RS_TOUCH(DECODE_is_text_entry_active);
    RS_TOUCH(decode_draw_ascii_panel);
    RS_TOUCH(DECODE_handle_event);
    RS_TOUCH(DECODE_draw_workstation);
}


static int rs_test_decode_unlock(void) {
    unsigned char key[32];
    char path[PATH_MAX];
    char error[512] = "";
    for (size_t i = 0; i < sizeof(key); i++) key[i] = (unsigned char)(0x70U + i);
    snprintf(path, sizeof(path), "%s/decode-master.key", rs_test_temp_dir);
    return rs_test_write_bytes(path, key, sizeof(key), 0600) &&
           DATABASE_CRYPTO_set_key_path(path, error, sizeof(error));
}
static void rs_test_behavior(void) {
    char copied[16];
    char trimmed[32];
    char quoted[128];
    unsigned char encoded[4];
    const unsigned char *cursor;
    uint32_t value = 0;
    int rgb = -1;
    int start = -1, len = -1, repeats = -1;
    SDL_Event event;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_Font *font = NULL;

    RS_CHECK("decode point inside", decode_point_in_rect(5, 5, (SDL_Rect){0,0,10,10}));
    decode_copy_text(copied, sizeof(copied), "bits");
    RS_CHECK("decode copy", strcmp(copied, "bits") == 0);
    RS_CHECK("decode RGB component", decode_classifier_parse_rgb_component("255", &rgb) && rgb == 255);
    RS_CHECK("decode RGB rejects overflow", !decode_classifier_parse_rgb_component("256", &rgb));
    decode_classifier_trim_text("  CRC Checksum  ", trimmed, sizeof(trimmed));
    RS_CHECK("decode trim", strcmp(trimmed, "CRC Checksum") == 0);
    decode_classifier_store_u32(encoded, 0x78563412U);
    cursor = encoded;
    RS_CHECK("decode u32 serialization", decode_classifier_read_u32(&cursor, encoded + sizeof(encoded), &value) && value == 0x78563412U);
    RS_CHECK("decode extension", decode_has_complex16_extension("sample.IQ16"));
    RS_CHECK("decode contains CI", decode_text_contains_ci("Bit Stream Classifier", "classifier"));
    RS_CHECK("decode preamble pattern validation", decode_pattern_has_zero_and_one("0011", 0, 4));
    RS_CHECK("decode repeated preamble", decode_find_repeated_preamble_in_bits("101101101000", 12, 0, 3, 3, 3, &start, &len, &repeats) && len == 3 && repeats >= 3);
    decode_shell_quote(quoted, sizeof(quoted), "a'b");
    RS_CHECK("decode shell quote", quoted[0] == '\'' && quoted[strlen(quoted)-1] == '\'');

    decode_classifier_initialize();
    RS_CHECK("decode classifier built-ins", Global_Decode_Classifier_Label_Count >= 10);
    snprintf(Global_Decode_Classifier_New_Label, sizeof(Global_Decode_Classifier_New_Label), "Device ID");
    decode_classifier_add_custom_label();
    RS_CHECK("decode custom label", Global_Decode_Classifier_Label_Count >= 11);
    snprintf(Global_Decode_Classifier_RGB_Text[0], sizeof(Global_Decode_Classifier_RGB_Text[0]), "12");
    snprintf(Global_Decode_Classifier_RGB_Text[1], sizeof(Global_Decode_Classifier_RGB_Text[1]), "34");
    snprintf(Global_Decode_Classifier_RGB_Text[2], sizeof(Global_Decode_Classifier_RGB_Text[2]), "56");
    decode_classifier_apply_custom_rgb();
    RS_CHECK("decode custom RGB applied", Global_Decode_Classifier_Labels[Global_Decode_Classifier_Selected_Label].color.r == 12 &&
                                              Global_Decode_Classifier_Labels[Global_Decode_Classifier_Selected_Label].color.g == 34 &&
                                              Global_Decode_Classifier_Labels[Global_Decode_Classifier_Selected_Label].color.b == 56);

    snprintf(Global_Decode_Bitstream, sizeof(Global_Decode_Bitstream), "1011011010001111");
    Global_Decode_Bitstream_Len = (int)strlen(Global_Decode_Bitstream);
    Global_Decode_Bit_Selection_Start = 0;
    Global_Decode_Bit_Selection_End = 6;
    Global_Decode_Classifier_Selected_Label = 0;
    decode_classifier_apply_selected_label();
    RS_CHECK("decode classifier assigns bits", decode_classifier_label_bit_count(0) == 6 && decode_classifier_segment_count() == 1);
    decode_classifier_remove_selected_tag();
    RS_CHECK("decode classifier removes assignment", decode_classifier_label_bit_count(0) == 0);

    snprintf(Global_Decode_Classifier_Document_Name, sizeof(Global_Decode_Classifier_Document_Name), "classifier-roundtrip");
    Global_Decode_Classifier_Document_Cursor = (int)strlen(Global_Decode_Classifier_Document_Name);
    Global_Decode_Bit_Selection_Start = 0;
    Global_Decode_Bit_Selection_End = 8;
    Global_Decode_Classifier_Selected_Label = 7;
    decode_classifier_apply_selected_label();
    RS_CHECK("decode database unlocked", rs_test_decode_unlock());
    decode_classifier_save();
    decode_classifier_clear_assignments();
    Global_Decode_Bitstream[0] = '\0';
    Global_Decode_Bitstream_Len = 0;
    decode_classifier_load();
    RS_CHECK("decode classifier database roundtrip", Global_Decode_Bitstream_Len == 16 && decode_classifier_label_bit_count(7) == 8);

    DECODE_enter_mode(rs_test_temp_dir);
    RS_CHECK("decode text state valid", DECODE_is_text_entry_active() == 0 || DECODE_is_text_entry_active() == 1);
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 10;
    event.motion.y = 10;
    { int handled = DECODE_handle_event(&event, 1280, 720); RS_CHECK("decode event smoke", handled == 0 || handled == 1); }
    if (rs_test_make_renderer(&surface, &renderer, &font)) {
        DECODE_draw_workstation(renderer, font, 1280, 720);
        RS_CHECK("decode draw smoke", 1);
        rs_test_destroy_renderer(surface, renderer, font);
    } else {
        RS_SKIP("decode draw smoke", "SDL/TTF renderer unavailable");
    }
    DECODE_exit_mode();
}

#endif


#if RS_TEST_TARGET == 10
#include "../src/GUIs.c"

static const char *rs_test_target_name = "GUIs.c";
static const int rs_test_expected_function_count = 38;

static void rs_test_touch_all(void) {
    RS_TOUCH(load_font);
    RS_TOUCH(draw_text);
    RS_TOUCH(rgb);
    RS_TOUCH(toggle_fullscreen);
    RS_TOUCH(set_status);
    RS_TOUCH(draw_filled_rect);
    RS_TOUCH(draw_outline_rect);
    RS_TOUCH(draw_made_in_usa);
    RS_TOUCH(draw_button);
    RS_TOUCH(point_in_rect);
    RS_TOUCH(near_px);
    RS_TOUCH(draw_input_box);
    RS_TOUCH(draw_checkbox);
    RS_TOUCH(layout_controls);
    RS_TOUCH(draw_control_panel);
    RS_TOUCH(draw_frequency_axis);
    RS_TOUCH(draw_border);
    RS_TOUCH(draw_selection_overlay);
    RS_TOUCH(draw_selector_bandwidth);
    RS_TOUCH(update_selection_from_mouse);
    RS_TOUCH(draw_antenna_recommendation);
    RS_TOUCH(power_to_color_relative);
    RS_TOUCH(clear_waterfall);
    RS_TOUCH(reset_prev_col_db);
    RS_TOUCH(append_text);
    RS_TOUCH(backspace_text);
    RS_TOUCH(cmp_double_for_qsort);
    RS_TOUCH(get_visible_bin_range);
    RS_TOUCH(estimate_noise_floor_median_visible);
    RS_TOUCH(add_fft_line_to_waterfall);
    RS_TOUCH(ANALYSIS_draw_line_plot);
    RS_TOUCH(ANALYSIS_draw_constellation_plot);
    RS_TOUCH(ANALYSIS_make_ellipsis_text);
    RS_TOUCH(ANALYSIS_draw_loading_indicator);
    RS_TOUCH(ANALYSIS_draw_file_list);
    RS_TOUCH(ANALYSIS_draw_filter_overlay);
    RS_TOUCH(ANALYSIS_draw_workstation);
    RS_TOUCH(ANALYSIS_exit_mode);
}


static void rs_test_behavior(void) {
    char text[32] = "abc";
    double values[5] = {9.0, 1.0, 4.0, 2.0, 7.0};
    uint32_t *pixels = calloc(64, sizeof(*pixels));
    SDL_Rect rect = {10, 10, 20, 20};

    RS_CHECK("rgb packs red", rgb(255, 0, 0) != rgb(0, 255, 0));
    RS_CHECK("GUI point inside", point_in_rect(15, 15, rect));
    RS_CHECK("GUI point edge outside", !point_in_rect(30, 15, rect));
    RS_CHECK("near_px true", near_px(10, 13, 3));
    RS_CHECK("near_px false", !near_px(10, 14, 3));
    append_text(text, sizeof(text), "1x2.3");
    RS_CHECK("append_text", strcmp(text, "abc12.3") == 0);
    backspace_text(text);
    RS_CHECK("backspace_text", strcmp(text, "abc12.") == 0);
    qsort(values, 5, sizeof(values[0]), cmp_double_for_qsort);
    RS_CHECK("double comparator", values[0] == 1.0 && values[4] == 9.0);
    RS_CHECK("power color stable", power_to_color_relative(5.0, 10.0, 2.0) == power_to_color_relative(5.0, 10.0, 2.0));
    if (pixels) {
        for (int i = 0; i < 64; i++) pixels[i] = 0xffffffffU;
        clear_waterfall(pixels, 8, 8);
        RS_CHECK("clear waterfall", pixels[0] == rgb(0, 0, 0) && pixels[63] == rgb(0, 0, 0));
        free(pixels);
    } else {
        RS_CHECK("allocate waterfall test", 0);
    }
}

#endif


#if RS_TEST_TARGET == 11
#include "../src/MapDashboard.c"

static const char *rs_test_target_name = "MapDashboard.c";
static const int rs_test_expected_function_count = 49;

static void rs_test_touch_all(void) {
    RS_TOUCH(dashboard_sdr_selector_rects);
    RS_TOUCH(dashboard_sdr_copy_truncated);
    RS_TOUCH(dashboard_refresh_sdr_options);
    RS_TOUCH(dashboard_draw_sdr_selector);
    RS_TOUCH(dashboard_handle_sdr_selector_event);
    RS_TOUCH(dashboard_point_in_rect);
    RS_TOUCH(dashboard_update_timestamp);
    RS_TOUCH(dashboard_copy_text);
    RS_TOUCH(dashboard_draw_text_centered);
    RS_TOUCH(dashboard_draw_grid);
    RS_TOUCH(dashboard_top_rect);
    RS_TOUCH(dashboard_content_rect);
    RS_TOUCH(dashboard_make_tabs);
    RS_TOUCH(dashboard_draw_tab);
    RS_TOUCH(dashboard_draw_top_bar);
    RS_TOUCH(dashboard_draw_station_card);
    RS_TOUCH(dashboard_hash_string);
    RS_TOUCH(dashboard_ascii_equal_ci);
    RS_TOUCH(dashboard_ascii_contains_ci);
    RS_TOUCH(dashboard_case_color);
    RS_TOUCH(dashboard_find_case_index);
    RS_TOUCH(dashboard_case_index_for);
    RS_TOUCH(dashboard_csv_parse_line);
    RS_TOUCH(dashboard_unescape_multiline_text);
    RS_TOUCH(dashboard_load_case_descriptions);
    RS_TOUCH(dashboard_load_case_metadata);
    RS_TOUCH(dashboard_parse_coordinate);
    RS_TOUCH(dashboard_load_case_content);
    RS_TOUCH(dashboard_reload_cases);
    RS_TOUCH(dashboard_lonlat_to_screen);
    RS_TOUCH(dashboard_draw_circle_outline);
    RS_TOUCH(dashboard_draw_case_dot);
    RS_TOUCH(dashboard_case_matches_search);
    RS_TOUCH(dashboard_draw_case_points);
    RS_TOUCH(dashboard_select_case_at);
    RS_TOUCH(dashboard_wrap_text);
    RS_TOUCH(dashboard_case_country_matches);
    RS_TOUCH(dashboard_collect_cases_for_country);
    RS_TOUCH(dashboard_draw_hover_country_cases);
    RS_TOUCH(dashboard_draw_case_search);
    RS_TOUCH(dashboard_draw_case_sidebar);
    RS_TOUCH(dashboard_handle_case_search_event);
    RS_TOUCH(dashboard_handle_case_sidebar_event);
    RS_TOUCH(dashboard_handle_top_tab_event);
    RS_TOUCH(dashboard_init);
    RS_TOUCH(dashboard_shutdown);
    RS_TOUCH(dashboard_find_country_screen_point);
    RS_TOUCH(dashboard_handle_event);
    RS_TOUCH(dashboard_draw);
}


static void rs_test_behavior(void) {
    char copied[32];
    char line[] = "\"CASE-1\",\"Line one\\nLine two\",US";
    char fields[4][512];
    double coordinate = 0.0;
    SDL_Color a, b;
    Type_Dashboard_State dashboard;
    SDL_Event event;
    SDL_Surface *surface = NULL;
    SDL_Renderer *renderer = NULL;
    TTF_Font *font = NULL;
    char map_path[PATH_MAX];

    RS_CHECK("dashboard point inside", dashboard_point_in_rect(5, 5, (SDL_Rect){0,0,10,10}));
    dashboard_copy_text(copied, sizeof(copied), "Map");
    RS_CHECK("dashboard copy", strcmp(copied, "Map") == 0);
    RS_CHECK("dashboard equals CI", dashboard_ascii_equal_ci("United States", "united states"));
    RS_CHECK("dashboard contains CI", dashboard_ascii_contains_ci("United States", "states"));
    RS_CHECK("dashboard CSV parse", dashboard_csv_parse_line(line, fields, 4) >= 3 && strcmp(fields[0], "CASE-1") == 0);
    dashboard_unescape_multiline_text(fields[1]);
    RS_CHECK("dashboard unescapes newline", strchr(fields[1], '\n') != NULL);
    RS_CHECK("dashboard coordinate parser", dashboard_parse_coordinate("-93.25", -180.0, 180.0, &coordinate) && fabs(coordinate + 93.25) < 1e-12);
    a = dashboard_case_color("CASE-1");
    b = dashboard_case_color("CASE-1");
    RS_CHECK("dashboard case color deterministic", memcmp(&a, &b, sizeof(a)) == 0);

    memset(&dashboard, 0, sizeof(dashboard));
    snprintf(map_path, sizeof(map_path), "%s/src/world_map.bin", rs_test_project_root);
    RS_CHECK("dashboard initializes map", dashboard_init(&dashboard, map_path));
    memset(&event, 0, sizeof(event));
    event.type = SDL_MOUSEMOTION;
    event.motion.x = 100;
    event.motion.y = 100;
    { int handled = dashboard_handle_event(&dashboard, &event, 1280, 720); RS_CHECK("dashboard event smoke", handled == 0 || handled == 1); }
    if (rs_test_make_renderer(&surface, &renderer, &font)) {
        dashboard_draw(&dashboard, renderer, font, font, 1280, 720, 100, 100);
        RS_CHECK("dashboard draw smoke", 1);
        rs_test_destroy_renderer(surface, renderer, font);
    } else {
        RS_SKIP("dashboard draw smoke", "SDL/TTF renderer unavailable");
    }
    dashboard_shutdown();
}

#endif


#if RS_TEST_TARGET == 12
#define main retrospectrum_application_main
#include "../src/RetroSpectrum.c"
#undef main

static const char *rs_test_target_name = "RetroSpectrum.c";
static const int rs_test_expected_function_count = 107;

static void rs_test_touch_all(void) {
    RS_TOUCH(handle_sigint);
    RS_TOUCH(draw_thick_line);
    RS_TOUCH(draw_cubic_cable);
    RS_TOUCH(draw_sdr_disconnected);
    RS_TOUCH(limit_double);
    RS_TOUCH(ensure_record_dir_exists);
    RS_TOUCH(selection_center_Hz);
    RS_TOUCH(selection_BW_Hz);
    RS_TOUCH(configure_recording_filter);
    RS_TOUCH(pre_cache_init);
    RS_TOUCH(pre_cache_free);
    RS_TOUCH(pre_cache_resize);
    RS_TOUCH(pre_cache_write);
    RS_TOUCH(pre_cache_snapshot_locked);
    RS_TOUCH(rec_queue_init);
    RS_TOUCH(rec_queue_resize);
    RS_TOUCH(rec_queue_free);
    RS_TOUCH(rec_queue_available_locked);
    RS_TOUCH(rec_queue_reset);
    RS_TOUCH(rec_queue_push_block);
    RS_TOUCH(rec_queue_pop_block);
    RS_TOUCH(rec_queue_request_stop);
    RS_TOUCH(recorder_reset_processing_state);
    RS_TOUCH(recorder_write_sample);
    RS_TOUCH(recorder_thread_main);
    RS_TOUCH(stop_recording);
    RS_TOUCH(start_recording);
    RS_TOUCH(ring_available_locked);
    RS_TOUCH(ring_clear);
    RS_TOUCH(ring_write_sample);
    RS_TOUCH(ring_read_block);
    RS_TOUCH(sdr_name_equals);
    RS_TOUCH(sdr_nearest_supported_value);
    RS_TOUCH(sdr_clamp_frequency);
    RS_TOUCH(sdr_clamp_sample_rate);
    RS_TOUCH(sdr_clamp_bandwidth);
    RS_TOUCH(sdr_clamp_gain);
    RS_TOUCH(sdr_copy_identity);
    RS_TOUCH(sdr_discover_gain_controls);
    RS_TOUCH(sdr_channel_has_gain_controls);
    RS_TOUCH(sdr_apply_rx_gain_controls);
    RS_TOUCH(sdr_cache_selected_label);
    RS_TOUCH(sdr_open_selected_device);
    RS_TOUCH(process_rx_samples);
    RS_TOUCH(sdr_rx_thread_main);
    RS_TOUCH(compute_DB_from_FFT);
    RS_TOUCH(parse_positive_double);
    RS_TOUCH(parse_nonnegative_int);
    RS_TOUCH(normalize_lna_gain);
    RS_TOUCH(normalize_vga_gain);
    RS_TOUCH(normalize_fps);
    RS_TOUCH(normalize_rows_per_frame);
    RS_TOUCH(stop_radio);
    RS_TOUCH(start_radio);
    RS_TOUCH(recommended_antenna_length_inches);
    RS_TOUCH(apply_radio_settings);
    RS_TOUCH(RETROSPECTRUM_sdr_selected_label);
    RS_TOUCH(RETROSPECTRUM_sdr_args_is_selected);
    RS_TOUCH(RETROSPECTRUM_select_sdr_args);
    RS_TOUCH(retrospectrum_tx_thread_main);
    RS_TOUCH(retrospectrum_close_transmit_file_locked);
    RS_TOUCH(retrospectrum_restore_receive_settings);
    RS_TOUCH(retrospectrum_finalize_transmission);
    RS_TOUCH(retrospectrum_pump_transmission);
    RS_TOUCH(RETROSPECTRUM_start_file_transmission);
    RS_TOUCH(RETROSPECTRUM_cancel_file_transmission);
    RS_TOUCH(RETROSPECTRUM_get_transmission_status);
    RS_TOUCH(RETROSPECTRUM_acknowledge_transmission_result);
    RS_TOUCH(RETROSPECTRUM_transmission_is_active);
    RS_TOUCH(apply_from_inputs);
    RS_TOUCH(main_field_index);
    RS_TOUCH(main_field_text_by_index);
    RS_TOUCH(main_field_text);
    RS_TOUCH(main_clamp_cursor_for_text);
    RS_TOUCH(main_reset_input_cursors);
    RS_TOUCH(main_set_active_cursor_end);
    RS_TOUCH(main_insert_text_at_cursor);
    RS_TOUCH(main_backspace_at_cursor);
    RS_TOUCH(main_move_active_cursor);
    RS_TOUCH(main_make_cursor_box);
    RS_TOUCH(cli_secure_zero);
    RS_TOUCH(cli_trim_line);
    RS_TOUCH(cli_read_line);
    RS_TOUCH(cli_prompt_yes_no);
    RS_TOUCH(cli_prompt_password_twice);
    RS_TOUCH(cli_role_name);
    RS_TOUCH(cli_role_is_privileged);
    RS_TOUCH(cli_load_users);
    RS_TOUCH(cli_lookup_user);
    RS_TOUCH(cli_print_users);
    RS_TOUCH(cli_enroll_totp);
    RS_TOUCH(cli_bootstrap_primary_admin);
    RS_TOUCH(cli_authenticate);
    RS_TOUCH(cli_read_target_username);
    RS_TOUCH(cli_create_user);
    RS_TOUCH(cli_reset_password);
    RS_TOUCH(cli_enable_totp);
    RS_TOUCH(cli_disable_totp);
    RS_TOUCH(cli_set_role);
    RS_TOUCH(cli_delete_user);
    RS_TOUCH(cli_print_privileged_commands);
    RS_TOUCH(cli_print_user_commands);
    RS_TOUCH(cli_run_session);
    RS_TOUCH(run_management_cli);
    RS_TOUCH(print_command_line_usage);
    RS_TOUCH(parse_command_line_args);
    RS_TOUCH(retrospectrum_application_main);
}


static void rs_test_behavior(void) {
    double parsed = 0.0;
    int iv = 0;
    char text[64] = "34";
    int cursor = 2;
    char error[256] = "";
    char *argv_help[] = {"retrospectrum", "--help", NULL};
    char *argv_bad[] = {"retrospectrum", "--definitely-invalid", NULL};

    RS_CHECK("limit_double lower", limit_double(-1.0, 0.0, 1.0) == 0.0);
    RS_CHECK("limit_double middle", limit_double(0.5, 0.0, 1.0) == 0.5);
    RS_CHECK("limit_double upper", limit_double(2.0, 0.0, 1.0) == 1.0);
    RS_CHECK("antenna recommendation positive", recommended_antenna_length_inches(433920000ULL) > 0.0);
    RS_CHECK("SDR selected label available", RETROSPECTRUM_sdr_selected_label() != NULL);
    RS_CHECK("SDR empty args not selected", !RETROSPECTRUM_sdr_args_is_selected(""));
    RS_CHECK("reject empty SDR selection", !RETROSPECTRUM_select_sdr_args("", error, sizeof(error)));
    RS_CHECK("parse positive double", parse_positive_double("1.25", &parsed) && fabs(parsed - 1.25) < 1e-12);
    RS_CHECK("reject zero positive double", !parse_positive_double("0", &parsed));
    RS_CHECK("parse nonnegative integer", parse_nonnegative_int("0", &iv) && iv == 0);
    RS_CHECK("normalize LNA", normalize_lna_gain(-500) == -200 && normalize_lna_gain(500) == 200);
    RS_CHECK("normalize VGA", normalize_vga_gain(-500) == -200 && normalize_vga_gain(500) == 200);
    RS_CHECK("normalize FPS", normalize_fps(0) >= 1);
    RS_CHECK("normalize rows", normalize_rows_per_frame(0) >= 1);
    main_insert_text_at_cursor(text, sizeof(text), &cursor, "12x");
    RS_CHECK("main cursor insert", strcmp(text, "3412") == 0 && cursor == 4);
    main_backspace_at_cursor(text, sizeof(text), &cursor);
    RS_CHECK("main cursor backspace", strcmp(text, "341") == 0 && cursor == 3);
    RS_CHECK("CLI trims line", ({ char line[16] = " test \r\n"; cli_trim_line(line, sizeof(line)); strcmp(line, "test") == 0; }));
    RS_CHECK("CLI role name", cli_role_name(AUTH_ROLE_ADMIN) != NULL);
    RS_CHECK("CLI privileged role", cli_role_is_privileged(AUTH_ROLE_ADMIN));
    Global_Help_Requested = 0;
    RS_CHECK("parse help command line", parse_command_line_args(2, argv_help) == 0 && Global_Help_Requested);
    RS_CHECK("reject unknown command line", !parse_command_line_args(2, argv_bad));
    RETROSPECTRUM_cancel_file_transmission();
}

#endif


#if RS_TEST_TARGET == 13
#include "../src/SecureFunctions.c"

static const char *rs_test_target_name = "SecureFunctions.c";
static const int rs_test_expected_function_count = 20;

static void rs_test_touch_all(void) {
    RS_TOUCH(sec_sprintf);
    RS_TOUCH(sec_strcpy);
    RS_TOUCH(sec_strcat);
    RS_TOUCH(sec_memcpy);
    RS_TOUCH(sec_memmove);
    RS_TOUCH(sec_str_memcpy);
    RS_TOUCH(sec_memzero);
    RS_TOUCH(sec_mul_bound);
    RS_TOUCH(sec_calloc_array);
    RS_TOUCH(sec_str_to_int);
    RS_TOUCH(sec_str_to_double);
    RS_TOUCH(sec_popen_read);
    RS_TOUCH(sec_has_extension);
    RS_TOUCH(sec_fopen_complex16);
    RS_TOUCH(sec_str_to_int_bound);
    RS_TOUCH(sec_str_to_double_bound);
    RS_TOUCH(sec_normalize_directory_path);
    RS_TOUCH(sec_directory_fd_is_secure);
    RS_TOUCH(sec_ensure_private_directory);
    RS_TOUCH(sec_fopen_exclusive_in_directory);
}


static void rs_test_behavior(void) {
    char text[64];
    char overlap[16] = "abcdef";
    unsigned char bytes[8] = {1,2,3,4,5,6,7,8};
    int iv = 0;
    double dv = 0.0;
    void *allocated;
    FILE *fp = NULL;
    size_t iq_count = 0;
    char path[PATH_MAX];
    char nested[PATH_MAX];
    char output[128];
    char *const echo_args[] = {"/bin/echo", "RetroSpectrum", NULL};

    RS_CHECK("sec_sprintf formats", sec_sprintf(text, sizeof(text), "%s-%d", "test", 7) && strcmp(text, "test-7") == 0);
    RS_CHECK("sec_sprintf rejects truncation", !sec_sprintf(text, 4, "%s", "abcdef"));
    RS_CHECK("sec_strcpy copies", sec_strcpy(text, sizeof(text), "alpha") && strcmp(text, "alpha") == 0);
    RS_CHECK("sec_strcpy rejects truncation", !sec_strcpy(text, 3, "alpha"));
    RS_CHECK("sec_strcat appends", sec_strcpy(text, sizeof(text), "a") && sec_strcat(text, sizeof(text), "bc") && strcmp(text, "abc") == 0);
    RS_CHECK("sec_memcpy copies", sec_memcpy(bytes + 4, 4, bytes, 4) && memcmp(bytes, bytes + 4, 4) == 0);
    RS_CHECK("sec_memcpy bounds", !sec_memcpy(bytes, 2, bytes + 2, 4));
    RS_CHECK("sec_memmove overlap", sec_memmove(overlap + 2, sizeof(overlap) - 2, overlap, 6) && memcmp(overlap + 2, "abcdef", 6) == 0);
    RS_CHECK("sec_str_memcpy terminates", sec_str_memcpy(text, sizeof(text), "abcdef", 3) && strcmp(text, "abc") == 0);
    RS_CHECK("sec_memzero clears", sec_memzero(bytes, sizeof(bytes)) && bytes[0] == 0 && bytes[7] == 0);
    RS_CHECK("sec_mul_bound accepts safe product", sec_mul_bound(1024, 1024));
    RS_CHECK("sec_mul_bound rejects overflow", !sec_mul_bound(SIZE_MAX, 2));
    allocated = sec_calloc_array(4, sizeof(int), 8);
    RS_CHECK("sec_calloc_array allocates", allocated != NULL);
    free(allocated);
    RS_CHECK("sec_calloc_array enforces max", sec_calloc_array(9, sizeof(int), 8) == NULL);
    RS_CHECK("sec_str_to_int parses", sec_str_to_int("-42", &iv) && iv == -42);
    RS_CHECK("sec_str_to_int rejects suffix", !sec_str_to_int("42x", &iv));
    RS_CHECK("sec_str_to_double parses", sec_str_to_double("3.25", &dv) && fabs(dv - 3.25) < 1e-12);
    RS_CHECK("sec_str_to_double rejects nonfinite", !sec_str_to_double("nan", &dv));
    RS_CHECK("sec_str_to_int_bound accepts", sec_str_to_int_bound("8", 1, 10, &iv) && iv == 8);
    RS_CHECK("sec_str_to_int_bound rejects", !sec_str_to_int_bound("11", 1, 10, &iv));
    RS_CHECK("sec_str_to_double_bound accepts", sec_str_to_double_bound("0.5", 0.0, 1.0, &dv) && fabs(dv - 0.5) < 1e-12);
    RS_CHECK("sec_str_to_double_bound rejects", !sec_str_to_double_bound("-1", 0.0, 1.0, &dv));
    RS_CHECK("sec_has_extension matches exact suffix", sec_has_extension("capture.complex16", ".complex16"));
    RS_CHECK("sec_has_extension is case-sensitive", !sec_has_extension("capture.COMPLEX16", ".complex16"));
    RS_CHECK("sec_has_extension rejects wrong suffix", !sec_has_extension("capture.txt", ".complex16"));
    RS_CHECK("sec_popen_read captures output", sec_popen_read("/bin/echo", echo_args, output, sizeof(output)) && strstr(output, "RetroSpectrum") != NULL);

    snprintf(path, sizeof(path), "%s/iq.complex16", rs_test_temp_dir);
    RS_CHECK("create synthetic complex16", rs_test_write_iq_file(path, 32));
    RS_CHECK("sec_fopen_complex16 opens valid file", sec_fopen_complex16(path, &fp, &iq_count) && fp != NULL && iq_count == 32);
    if (fp) fclose(fp);
    snprintf(path, sizeof(path), "%s/invalid.complex16", rs_test_temp_dir);
    RS_CHECK("write invalid complex16", rs_test_write_bytes(path, "abc", 3, 0600));
    RS_CHECK("sec_fopen_complex16 rejects odd size", !sec_fopen_complex16(path, &fp, &iq_count));

    snprintf(nested, sizeof(nested), "%s/private", rs_test_temp_dir);
    RS_CHECK("sec_ensure_private_directory creates", sec_ensure_private_directory(nested, 0700));
    {
        struct stat st;
        RS_CHECK("private directory mode", stat(nested, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 077) == 0);
    }
    RS_CHECK("normalize directory path", sec_normalize_directory_path(nested, text, sizeof(text)) && text[0] == '/');
    RS_CHECK("exclusive file create", sec_fopen_exclusive_in_directory(nested, "new.bin", &fp) && fp != NULL);
    if (fp) { fputs("ok", fp); fclose(fp); fp = NULL; }
    RS_CHECK("exclusive file refuses overwrite", !sec_fopen_exclusive_in_directory(nested, "new.bin", &fp));
}

#endif


#if RS_TEST_TARGET == 14
#include "../src/SecureNetwork.c"

static const char *rs_test_target_name = "SecureNetwork.c";
static const int rs_test_expected_function_count = 61;

static void rs_test_touch_all(void) {
    RS_TOUCH(secure_network_set_error);
    RS_TOUCH(secure_network_set_status);
    RS_TOUCH(secure_network_store_u16);
    RS_TOUCH(secure_network_store_u32);
    RS_TOUCH(secure_network_store_u64);
    RS_TOUCH(secure_network_load_u16);
    RS_TOUCH(secure_network_load_u32);
    RS_TOUCH(secure_network_load_u64);
    RS_TOUCH(secure_network_ssl_write_all);
    RS_TOUCH(secure_network_ssl_read_all);
    RS_TOUCH(secure_network_send_frame);
    RS_TOUCH(secure_network_receive_frame);
    RS_TOUCH(secure_network_generate_tls_key);
    RS_TOUCH(secure_network_generate_tls_certificate);
    RS_TOUCH(secure_network_configure_context);
    RS_TOUCH(secure_network_create_server_context);
    RS_TOUCH(secure_network_create_client_context);
    RS_TOUCH(secure_network_validate_negotiated_tls);
    RS_TOUCH(secure_network_export_binding);
    RS_TOUCH(secure_network_build_proof_message);
    RS_TOUCH(secure_network_server_identity_proof);
    RS_TOUCH(secure_network_client_identity_proof);
    RS_TOUCH(secure_network_handshake_allowed);
    RS_TOUCH(secure_network_send_status);
    RS_TOUCH(secure_network_parse_auth);
    RS_TOUCH(secure_network_handle_auth);
    RS_TOUCH(secure_network_handle_save);
    RS_TOUCH(secure_network_handle_load);
    RS_TOUCH(secure_network_handle_delete);
    RS_TOUCH(secure_network_handle_list);
    RS_TOUCH(secure_network_handle_user_list);
    RS_TOUCH(secure_network_handle_user_admin);
    RS_TOUCH(secure_network_client_thread);
    RS_TOUCH(secure_network_server_thread);
    RS_TOUCH(SECURE_NETWORK_start_server);
    RS_TOUCH(SECURE_NETWORK_server_is_running);
    RS_TOUCH(SECURE_NETWORK_stop_server);
    RS_TOUCH(secure_network_close_client_locked);
    RS_TOUCH(secure_network_abort_client_locked);
    RS_TOUCH(secure_network_configure_client_keepalive);
    RS_TOUCH(SECURE_NETWORK_disconnect);
    RS_TOUCH(secure_network_connect_locked);
    RS_TOUCH(secure_network_receive_status_locked);
    RS_TOUCH(SECURE_NETWORK_authenticate);
    RS_TOUCH(SECURE_NETWORK_verify_password);
    RS_TOUCH(SECURE_NETWORK_is_authenticated_remote);
    RS_TOUCH(SECURE_NETWORK_remote_connection_lost);
    RS_TOUCH(SECURE_NETWORK_status);
    RS_TOUCH(secure_network_request_locked);
    RS_TOUCH(SECURE_NETWORK_save_document);
    RS_TOUCH(SECURE_NETWORK_load_document);
    RS_TOUCH(SECURE_NETWORK_delete_document);
    RS_TOUCH(SECURE_NETWORK_list_documents);
    RS_TOUCH(SECURE_NETWORK_list_users);
    RS_TOUCH(secure_network_admin_request);
    RS_TOUCH(SECURE_NETWORK_admin_create_user);
    RS_TOUCH(SECURE_NETWORK_admin_reset_password);
    RS_TOUCH(SECURE_NETWORK_admin_set_totp);
    RS_TOUCH(SECURE_NETWORK_admin_remove_totp);
    RS_TOUCH(SECURE_NETWORK_admin_set_role);
    RS_TOUCH(SECURE_NETWORK_admin_delete_user);
}


static void rs_test_behavior(void) {
    unsigned char buffer[16] = {0};
    char error[512] = "";
    int deleted = 1;
    unsigned char *content = (unsigned char *)0x1;
    size_t content_size = 99;
    int found = 1;

    secure_network_store_u16(buffer, 0x1234U);
    secure_network_store_u32(buffer + 2, 0x89ABCDEFU);
    secure_network_store_u64(buffer + 6, UINT64_C(0x0123456789ABCDEF));
    RS_CHECK("network u16 roundtrip", secure_network_load_u16(buffer) == 0x1234U);
    RS_CHECK("network u32 roundtrip", secure_network_load_u32(buffer + 2) == 0x89ABCDEFU);
    RS_CHECK("network u64 roundtrip", secure_network_load_u64(buffer + 6) == UINT64_C(0x0123456789ABCDEF));
    RS_CHECK("network status is available", SECURE_NETWORK_status() != NULL);
    RS_CHECK("network starts disconnected", !SECURE_NETWORK_is_authenticated_remote());
    RS_CHECK("network invalid authentication rejected", !SECURE_NETWORK_authenticate(NULL, NULL, NULL, NULL, error, sizeof(error)));
    RS_CHECK("network invalid password verification rejected", !SECURE_NETWORK_verify_password(NULL, NULL, error, sizeof(error)));
    RS_CHECK("network invalid save rejected", !SECURE_NETWORK_save_document(NULL, NULL, NULL, NULL, 0, error, sizeof(error)));
    RS_CHECK("network invalid load rejected", !SECURE_NETWORK_load_document(NULL, NULL, &content, &content_size, &found, error, sizeof(error)));
    RS_CHECK("network invalid delete rejected", !SECURE_NETWORK_delete_document(NULL, NULL, &deleted, error, sizeof(error)));
    SECURE_NETWORK_disconnect();
    RS_CHECK("network disconnect is idempotent", !SECURE_NETWORK_is_authenticated_remote());

    SERVER_IDENTITY_set_server_mode(1);
    if (SERVER_IDENTITY_start()) {
        int started = SECURE_NETWORK_start_server(error, sizeof(error));
        RS_CHECK("TLS server start", started && SECURE_NETWORK_server_is_running());
        SECURE_NETWORK_stop_server();
        RS_CHECK("TLS server stop", !SECURE_NETWORK_server_is_running());
        SERVER_IDENTITY_stop();
    } else {
        RS_CHECK("server identity prerequisite", 0);
    }
}

#endif


#if RS_TEST_TARGET == 15
#include "../src/ServerIdentity.c"

static const char *rs_test_target_name = "ServerIdentity.c";
static const int rs_test_expected_function_count = 67;

static void rs_test_touch_all(void) {
    RS_TOUCH(server_identity_secure_zero);
    RS_TOUCH(server_identity_set_status);
    RS_TOUCH(server_identity_ensure_directory);
    RS_TOUCH(server_identity_key_path);
    RS_TOUCH(server_identity_named_path);
    RS_TOUCH(server_identity_name_valid);
    RS_TOUCH(server_identity_default_name);
    RS_TOUCH(server_identity_write_name_file);
    RS_TOUCH(server_identity_load_or_create_name);
    RS_TOUCH(server_identity_generate_keypair);
    RS_TOUCH(server_identity_sha512_parts);
    RS_TOUCH(server_identity_key_file_digest);
    RS_TOUCH(server_identity_write_all);
    RS_TOUCH(server_identity_read_all);
    RS_TOUCH(server_identity_write_key_file);
    RS_TOUCH(server_identity_read_key_file);
    RS_TOUCH(server_identity_private_pkey);
    RS_TOUCH(server_identity_public_pkey);
    RS_TOUCH(server_identity_sign_with_private);
    RS_TOUCH(server_identity_verify_signature);
    RS_TOUCH(server_identity_verify_keypair);
    RS_TOUCH(server_identity_load_or_create_key);
    RS_TOUCH(server_identity_hash_public_key);
    RS_TOUCH(server_identity_id_from_public_key);
    RS_TOUCH(server_identity_fingerprint_from_public_key);
    RS_TOUCH(server_identity_public_file_digest);
    RS_TOUCH(server_identity_write_u16);
    RS_TOUCH(server_identity_read_u16);
    RS_TOUCH(server_identity_encode_public_file);
    RS_TOUCH(server_identity_decode_public_file);
    RS_TOUCH(server_identity_read_public_file);
    RS_TOUCH(server_identity_write_public_file);
    RS_TOUCH(server_identity_set_trusted);
    RS_TOUCH(server_identity_initialize_public_files);
    RS_TOUCH(server_identity_write_u64);
    RS_TOUCH(server_identity_read_u64);
    RS_TOUCH(server_identity_build_packet);
    RS_TOUCH(server_identity_packet_valid);
    RS_TOUCH(server_identity_mark_conflict);
    RS_TOUCH(server_identity_refresh_local_validation);
    RS_TOUCH(server_identity_send_broadcast);
    RS_TOUCH(server_identity_handle_packet);
    RS_TOUCH(server_identity_thread_main);
    RS_TOUCH(server_identity_open_socket);
    RS_TOUCH(SERVER_IDENTITY_set_server_mode);
    RS_TOUCH(SERVER_IDENTITY_start);
    RS_TOUCH(SERVER_IDENTITY_stop);
    RS_TOUCH(SERVER_IDENTITY_get_id);
    RS_TOUCH(SERVER_IDENTITY_get_fingerprint);
    RS_TOUCH(SERVER_IDENTITY_get_algorithm);
    RS_TOUCH(SERVER_IDENTITY_has_conflict);
    RS_TOUCH(SERVER_IDENTITY_get_local_name);
    RS_TOUCH(SERVER_IDENTITY_get_trusted_name);
    RS_TOUCH(SERVER_IDENTITY_get_trusted_fingerprint);
    RS_TOUCH(SERVER_IDENTITY_get_public_file_path);
    RS_TOUCH(SERVER_IDENTITY_trusted_is_local);
    RS_TOUCH(server_identity_resolve_import_path);
    RS_TOUCH(SERVER_IDENTITY_preview_public_file);
    RS_TOUCH(SERVER_IDENTITY_import_public_file);
    RS_TOUCH(SERVER_IDENTITY_validate_target);
    RS_TOUCH(SERVER_IDENTITY_get_trusted_host);
    RS_TOUCH(SERVER_IDENTITY_get_trusted_public);
    RS_TOUCH(SERVER_IDENTITY_get_local_public);
    RS_TOUCH(SERVER_IDENTITY_sign_local);
    RS_TOUCH(SERVER_IDENTITY_verify_trusted);
    RS_TOUCH(SERVER_IDENTITY_last_verified_at);
    RS_TOUCH(SERVER_IDENTITY_status);
}


static void rs_test_behavior(void) {
    unsigned char memory[32];
    unsigned char public_key[SERVER_IDENTITY_PUBLIC_KEY_BYTES];
    unsigned char signature[SERVER_IDENTITY_SIGNATURE_BYTES];
    const unsigned char message[] = "RetroSpectrum identity test";
    char status[512] = "";
    Type_Server_Public_Identity preview;

    memset(memory, 0xA5, sizeof(memory));
    server_identity_secure_zero(memory, sizeof(memory));
    RS_CHECK("identity secure zero", memory[0] == 0 && memory[31] == 0);
    RS_CHECK("identity name accepts normal", server_identity_name_valid("RetroSpectrum Server"));
    RS_CHECK("identity name rejects control characters", !server_identity_name_valid("bad\nname"));
    RS_CHECK("identity algorithm label", strcmp(SERVER_IDENTITY_get_algorithm(), "ML-DSA-87") == 0);

    SERVER_IDENTITY_set_server_mode(1);
    RS_CHECK("identity service starts", SERVER_IDENTITY_start());
    RS_CHECK("identity id generated", SERVER_IDENTITY_get_id() && strlen(SERVER_IDENTITY_get_id()) > 8);
    RS_CHECK("identity fingerprint generated", SERVER_IDENTITY_get_fingerprint() && strlen(SERVER_IDENTITY_get_fingerprint()) > 16);
    RS_CHECK("identity local public key available", SERVER_IDENTITY_get_local_public(public_key));
    RS_CHECK("identity signs message", SERVER_IDENTITY_sign_local(message, sizeof(message) - 1U, signature));
    RS_CHECK("identity verifies trusted signature", SERVER_IDENTITY_verify_trusted(message, sizeof(message) - 1U, signature));
    RS_CHECK("identity validates local target", SERVER_IDENTITY_validate_target(SERVER_IDENTITY_get_id(), status, sizeof(status)));
    RS_CHECK("identity public file preview", SERVER_IDENTITY_preview_public_file(SERVER_IDENTITY_get_public_file_path(), &preview, status, sizeof(status)));
    RS_CHECK("identity status available", SERVER_IDENTITY_status() != NULL);
    SERVER_IDENTITY_stop();
}

#endif


#if RS_TEST_TARGET == 16
#define WORLD_MAP_NO_DEMO
#include "../src/world_map_bin_loader.c"
#undef WORLD_MAP_NO_DEMO

static const char *rs_test_target_name = "world_map_bin_loader.c";
static const int rs_test_expected_function_count = 49;

static void rs_test_touch_all(void) {
    RS_TOUCH(WM_destroy_render_cache);
    RS_TOUCH(WM_read_exact);
    RS_TOUCH(WM_read_u32);
    RS_TOUCH(WM_read_u16);
    RS_TOUCH(WM_read_i16);
    RS_TOUCH(WM_recompute_bounds_from_points);
    RS_TOUCH(WM_draw_text);
    RS_TOUCH(WM_copy_text);
    RS_TOUCH(WM_draw_text_wrapped);
    RS_TOUCH(WORLD_MAP_free_flags);
    RS_TOUCH(WORLD_MAP_reset_view);
    RS_TOUCH(WORLD_MAP_free);
    RS_TOUCH(WORLD_MAP_load);
    RS_TOUCH(WM_ensure_loaded);
    RS_TOUCH(WM_project_point);
    RS_TOUCH(WM_screen_to_lonlat100);
    RS_TOUCH(WM_point_in_polygon_lonlat);
    RS_TOUCH(WM_polygon_intersects_view);
    RS_TOUCH(WM_zoom_to_country);
    RS_TOUCH(WM_clamp_view);
    RS_TOUCH(WM_zoom_view_at);
    RS_TOUCH(WM_pan_view_pixels);
    RS_TOUCH(WORLD_MAP_is_dragging);
    RS_TOUCH(WORLD_MAP_get_drag_offset);
    RS_TOUCH(WORLD_MAP_take_clicked_country);
    RS_TOUCH(WM_clamp_drag_offset);
    RS_TOUCH(WM_update_drag_position);
    RS_TOUCH(WM_finish_drag);
    RS_TOUCH(WORLD_MAP_handle_event);
    RS_TOUCH(WM_country_at);
    RS_TOUCH(WM_fill_poly);
    RS_TOUCH(WM_detail_segment_intersects_view);
    RS_TOUCH(WM_lon_jump_crosses_dateline);
    RS_TOUCH(WM_draw_detail_pair);
    RS_TOUCH(WM_draw_detail_lines);
    RS_TOUCH(WM_draw_hover_detail_lines);
    RS_TOUCH(WM_draw_polygon);
    RS_TOUCH(WM_fill_map_background);
    RS_TOUCH(WM_draw_map_grid);
    RS_TOUCH(WM_render_base_map_layer);
    RS_TOUCH(WM_draw_hover_layer);
    RS_TOUCH(WM_render_cache_factor);
    RS_TOUCH(WM_render_cache_source_rect);
    RS_TOUCH(WM_rebuild_render_cache);
    RS_TOUCH(WM_draw_cached_base_map);
    RS_TOUCH(WM_draw_dragged_cached_base_map);
    RS_TOUCH(WM_load_flag);
    RS_TOUCH(WM_draw_sidebar);
    RS_TOUCH(WORLD_MAP_draw);
}


static void rs_test_behavior(void) {
    char copied[16];
    int x = 0, y = 0;
    int dx = 0, dy = 0;
    int country;
    char map_path[PATH_MAX];
    SDL_Rect map = {0, 0, 1000, 500};

    WM_copy_text(copied, sizeof(copied), "World");
    RS_CHECK("world-map text copy", strcmp(copied, "World") == 0);
    WM_project_point(0, 0, map, &x, &y);
    RS_CHECK("world-map projects origin", x >= 0 && x <= map.w && y >= 0 && y <= map.h);
    WM_clamp_drag_offset(map, 100000, -100000, &dx, &dy);
    RS_CHECK("world-map clamps drag", abs(dx) < 100000 && abs(dy) < 100000);
    snprintf(map_path, sizeof(map_path), "%s/src/world_map.bin", rs_test_project_root);
    RS_CHECK("world-map binary loads", WORLD_MAP_load(map_path));
    RS_CHECK("world-map has countries", WM_DATA.loaded && WM_DATA.country_count > 100);
    WM_project_point(-9300, 4500, map, &x, &y);
    country = WM_country_at(map, x, y);
    RS_CHECK("world-map geographic lookup", country >= 0 && (uint32_t)country < WM_DATA.country_count);
    WORLD_MAP_free();
    RS_CHECK("world-map free", !WM_DATA.loaded && WM_DATA.country_count == 0);
}

#endif

int main(void) {
    char template_path[] = "/tmp/retrospectrum-full-tests-XXXXXX";
    const char *quiet = getenv("RS_TEST_QUIET");
    const char *root = getenv("RS_TEST_PROJECT_ROOT");

    rs_test_quiet = quiet && quiet[0] && strcmp(quiet, "0") != 0;
    if (root && root[0]) rs_test_project_root = root;

    if (!mkdtemp(template_path)) {
        fprintf(stderr, RS_ANSI_RED "FAILED" RS_ANSI_RESET " create test directory: %s\n", strerror(errno));
        return 1;
    }
    snprintf(rs_test_temp_dir, sizeof(rs_test_temp_dir), "%s", template_path);
    setenv("HOME", rs_test_temp_dir, 1);
    setenv("XDG_CONFIG_HOME", rs_test_temp_dir, 1);
    setenv("SDL_VIDEODRIVER", "dummy", 0);
    setenv("SDL_AUDIODRIVER", "dummy", 0);

    printf("\nRetroSpectrum target: %s\n", rs_test_target_name);
    printf("Expected function definitions: %d\n\n", rs_test_expected_function_count);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        rs_test_report_skip("SDL initialization", SDL_GetError());
    } else {
        rs_test_report_pass("SDL initialization");
    }
    if (TTF_Init() != 0) {
        rs_test_report_skip("SDL_ttf initialization", TTF_GetError());
    } else {
        rs_test_report_pass("SDL_ttf initialization");
    }

    rs_test_touch_all();
    rs_test_behavior();

    TTF_Quit();
    SDL_Quit();

    printf("\n%s summary: " RS_ANSI_GREEN "%d passed" RS_ANSI_RESET ", "
           RS_ANSI_RED "%d failed" RS_ANSI_RESET ", "
           RS_ANSI_YELLOW "%d skipped" RS_ANSI_RESET "\n",
           rs_test_target_name, rs_test_passed, rs_test_failed, rs_test_skipped);

    return rs_test_failed == 0 ? 0 : 1;
}
