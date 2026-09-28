#include "report.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shlwapi.h>

#include "compare.h"
#include "ranking.h"

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    BOOL failed;
} report_buffer_t;

static BOOL reserve(report_buffer_t *buffer, size_t extra)
{
    size_t needed, capacity;
    char *next;
    if (buffer->failed || extra > SIZE_MAX - buffer->length - 1) {
        buffer->failed = TRUE;
        return FALSE;
    }
    needed = buffer->length + extra + 1;
    if (needed <= buffer->capacity)
        return TRUE;
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    next = (char *)realloc(buffer->data, capacity);
    if (!next) {
        buffer->failed = TRUE;
        return FALSE;
    }
    buffer->data = next;
    buffer->capacity = capacity;
    return TRUE;
}

static BOOL append_n(report_buffer_t *buffer, const char *text, size_t length)
{
    if (!text || !reserve(buffer, length))
        return FALSE;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return TRUE;
}

static BOOL append(report_buffer_t *buffer, const char *text)
{
    return text ? append_n(buffer, text, strlen(text)) : FALSE;
}

static BOOL appendf(report_buffer_t *buffer, const char *format, ...)
{
    va_list args, copy;
    int length;
    char *text;
    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(args);
        buffer->failed = TRUE;
        return FALSE;
    }
    text = (char *)malloc((size_t)length + 1);
    if (!text) {
        va_end(args);
        buffer->failed = TRUE;
        return FALSE;
    }
    vsnprintf(text, (size_t)length + 1, format, args);
    va_end(args);
    append_n(buffer, text, (size_t)length);
    free(text);
    return !buffer->failed;
}

char *Report_AnsiToUtf8(const char *ansi_str)
{
    int source_length, wide_length, utf8_length;
    wchar_t *wide = NULL;
    char *utf8 = NULL;
    if (!ansi_str)
        return NULL;
    source_length = (int)strlen(ansi_str);
    if ((size_t)source_length != strlen(ansi_str))
        return NULL;
    wide_length = MultiByteToWideChar(CP_ACP, 0, ansi_str, source_length,
                                      NULL, 0);
    if (source_length > 0 && wide_length <= 0)
        return NULL;
    wide = (wchar_t *)malloc(((size_t)wide_length + 1) * sizeof(*wide));
    if (!wide)
        return NULL;
    if (wide_length > 0 &&
        MultiByteToWideChar(CP_ACP, 0, ansi_str, source_length, wide,
                            wide_length) != wide_length) {
        free(wide);
        return NULL;
    }
    wide[wide_length] = L'\0';
    utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, wide_length,
                                      NULL, 0, NULL, NULL);
    if (wide_length > 0 && utf8_length <= 0) {
        free(wide);
        return NULL;
    }
    utf8 = (char *)malloc((size_t)utf8_length + 1);
    if (utf8 && utf8_length > 0 &&
        WideCharToMultiByte(CP_UTF8, 0, wide, wide_length, utf8,
                            utf8_length, NULL, NULL) != utf8_length) {
        free(utf8);
        utf8 = NULL;
    }
    if (utf8)
        utf8[utf8_length] = '\0';
    free(wide);
    return utf8;
}

static BOOL append_html_text(report_buffer_t *buffer, const char *ansi)
{
    char *utf8 = Report_AnsiToUtf8(ansi ? ansi : "");
    const char *cursor;
    if (!utf8)
        return FALSE;
    for (cursor = utf8; *cursor; cursor++) {
        const char *entity = NULL;
        switch (*cursor) {
        case '&': entity = "&amp;"; break;
        case '<': entity = "&lt;"; break;
        case '>': entity = "&gt;"; break;
        case '"': entity = "&quot;"; break;
        case '\'': entity = "&#39;"; break;
        default:
            if (!append_n(buffer, cursor, 1)) {
                free(utf8);
                return FALSE;
            }
            continue;
        }
        if (!append(buffer, entity)) {
            free(utf8);
            return FALSE;
        }
    }
    free(utf8);
    return TRUE;
}

char *Report_Base64Encode(const BYTE *data, size_t input_len, size_t *out_len)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t encoded_length, i, output = 0;
    char *result;
    if (out_len)
        *out_len = 0;
    size_t groups;
    if (!data && input_len)
        return NULL;
    groups = input_len / 3 + (input_len % 3 != 0);
    if (groups > (SIZE_MAX - 1) / 4)
        return NULL;
    encoded_length = groups * 4;
    result = (char *)malloc(encoded_length + 1);
    if (!result)
        return NULL;
    for (i = 0; i < input_len; i += 3) {
        unsigned int value = (unsigned int)data[i] << 16;
        size_t remaining = input_len - i;
        if (remaining > 1)
            value |= (unsigned int)data[i + 1] << 8;
        if (remaining > 2)
            value |= data[i + 2];
        result[output++] = alphabet[(value >> 18) & 63];
        result[output++] = alphabet[(value >> 12) & 63];
        result[output++] = remaining > 1 ? alphabet[(value >> 6) & 63] : '=';
        result[output++] = remaining > 2 ? alphabet[value & 63] : '=';
    }
    result[output] = '\0';
    if (out_len)
        *out_len = output;
    return result;
}

static BOOL append_row(report_buffer_t *buffer, const char *label,
                       double value, const char *format)
{
    return appendf(buffer, "<tr><th>%s</th><td>", label) &&
           appendf(buffer, format, value) &&
           append(buffer, "</td></tr>");
}

static BOOL append_rank_summary(report_buffer_t *buffer,
                                const metrics_item_result_t *items, int count)
{
    static const char *names[4] = { "Detail", "Noise", "Color", "Tonal" };
    int i, c;
    if (!append(buffer, "<section class=\"item\"><h2>Metric Set v2 ranking"
                "</h2><p>Balanced profile; &asymp; indicates a JND tie.</p>"
                "<table><tr><th>Rank / image</th><th>Score</th>"))
        return FALSE;
    for (c = 0; c < 4; c++)
        if (!appendf(buffer, "<th>%s</th>", names[c])) return FALSE;
    if (!append(buffer, "</tr>")) return FALSE;
    for (i = 0; i < count; i++) {
        if (!appendf(buffer, "<tr><td>%d%s%s%s / ", items[i].rank_order,
                     items[i].rank_tied ? " &asymp;" : "",
                     items[i].rank_order == 1 ? " [TOP]" : "",
                     items[i].rank_order == count ? " [LAST]" : "") ||
            !append_html_text(buffer, items[i].image_name) ||
            !append(buffer, "</td>"))
            return FALSE;
        if (isfinite(items[i].rank_score)) {
            int red = (int)(2.0 * items[i].rank_score);
            int green = (int)(2.0 * (100.0 - items[i].rank_score));
            if (!appendf(buffer, "<td style=\"background:rgb(%d,%d,40)\">"
                         "%.1f</td>", red, green, items[i].rank_score))
                return FALSE;
        } else if (!append(buffer, "<td>N/A</td>")) return FALSE;
        for (c = 0; c < 4; c++) {
            double value = items[i].rank_category[c];
            if (!isfinite(value)) {
                if (!append(buffer, "<td>N/A</td>")) return FALSE;
            } else if (!appendf(buffer,
                        "<td style=\"background:rgba(%d,%d,80,.55)\">%.1f</td>",
                        (int)(2.0 * value), (int)(2.0 * (100.0 - value)),
                        value)) return FALSE;
        }
    }
    if (!append(buffer, "</table><h3>Category radar</h3><svg "
                "viewBox=\"0 0 400 300\" style=\"max-width:500px;width:100%\">"
                "<polygon points=\"200,25 320,145 200,265 80,145\" "
                "fill=\"none\" stroke=\"#71818c\"/>"))
        return FALSE;
    for (i = 0; i < count; i++) {
        double d[4];
        int color = i == 0 ? 82 : (i == 1 ? 255 : (i == 2 ? 214 : 142));
        for (c = 0; c < 4; c++)
            d[c] = isfinite(items[i].rank_category[c]) ?
                fmax(0.0, fmin(100.0, items[i].rank_category[c])) : 0.0;
        if (!appendf(buffer, "<polygon points=\"200,%.1f %.1f,145 "
                     "200,%.1f %.1f,145\" fill=\"#%02x9ee8\" "
                     "fill-opacity=\".12\" stroke=\"#%02x9ee8\"/>",
                     145.0 - d[0] * 1.2, 200.0 + d[1] * 1.2,
                     145.0 + d[2] * 1.2, 200.0 - d[3] * 1.2,
                     color, color))
            return FALSE;
    }
    return append(buffer, "<text x=\"184\" y=\"16\">Detail</text>"
                  "<text x=\"326\" y=\"149\">Noise</text>"
                  "<text x=\"184\" y=\"292\">Color</text>"
                  "<text x=\"20\" y=\"149\">Tonal</text></svg></section>");
}

static BOOL append_v2_details(report_buffer_t *buffer,
                              const metrics_item_result_t *item)
{
    static const char *names[METRICS_V2_COUNT] = {
        "S1 edge gain", "S2 edge transition width", "S3 overshoot",
        "S3 undershoot", "S4 low-tier gain", "S4 mid-tier gain",
        "S4 high-tier gain", "S4 tier ratio", "S5 low survival",
        "S5 mid survival", "S5 high survival", "N1 noise sigma",
        "N1 SNR dB", "N2 residual FWHM", "N3 shadow sigma",
        "C1 chroma sigma", "C2 chroma sigma", "C2 blotch FWHM",
        "C3 gray cast", "K1 chroma mean", "K1 chroma P95",
        "K1 chroma delta %", "K2 hue delta"
    };
    int i;
    if (!append(buffer, "<details><summary>Metric Set v2 details</summary>"
                "<table><tr><th>Metric</th><th>Value</th><th>Status</th></tr>"))
        return FALSE;
    for (i = 0; i < METRICS_V2_COUNT; i++) {
        if (!appendf(buffer, "<tr><th>%s</th><td>", names[i]))
            return FALSE;
        if (isfinite(item->v2[i])) {
            if (!appendf(buffer, "%.4f</td><td>OK</td></tr>", item->v2[i]))
                return FALSE;
        } else if (!append(buffer, "N/A</td><td>") ||
                   !append_html_text(buffer, item->v2_reason[i]) ||
                   !append(buffer, "</td></tr>")) return FALSE;
    }
    if (!append(buffer, "</table><table><tr><th>Five-band mean L*</th>"))
        return FALSE;
    for (i = 0; i < 5; i++) {
        if (isfinite(item->s2.gamma_l[i])) {
            if (!appendf(buffer, "<td>%d: %.2f</td>", i + 1,
                         item->s2.gamma_l[i])) return FALSE;
        } else if (!appendf(buffer, "<td>%d: N/A (", i + 1) ||
                   !append_html_text(buffer, item->s2.gamma_reason[i]) ||
                   !append(buffer, ")</td>")) return FALSE;
    }
    if (!append(buffer, "</tr></table>")) return FALSE;
    if (item->has_stage2) {
        static const char *alt[3] = {
            "edge contrast tiers", "noise residual", "gamma boosted"
        };
        for (i = 0; i < 3; i++)
            if (item->s2.v2_png_base64[i] &&
                (!appendf(buffer, "<img class=\"edge\" alt=\"%s preview\" "
                         "src=\"data:image/png;base64,", alt[i]) ||
                 !append_n(buffer, item->s2.v2_png_base64[i],
                           item->s2.v2_png_base64_len[i]) ||
                 !append(buffer, "\">"))) return FALSE;
    }
    return append(buffer, "</details>");
}

static BOOL append_warnings(report_buffer_t *buffer,
                            const metrics_item_result_t *items, int count)
{
    BOOL opened = FALSE;
    int i, metric;
    for (i = 0; i < count; i++) {
        for (metric = 0; metric < METRICS_V2_COUNT; metric++) {
            BOOL unavailable = !isfinite(items[i].v2[metric]);
            BOOL out_of_range = MetricsRank_OutOfRange(
                metric, items[i].v2[metric]);
            if (!unavailable && !out_of_range) continue;
            if (!opened) {
                if (!append(buffer, "<section class=\"item\"><h2>Warnings"
                            "</h2><ul>")) return FALSE;
                opened = TRUE;
            }
            if (!append(buffer, "<li>") ||
                !append_html_text(buffer, items[i].image_name) ||
                !appendf(buffer, ": metric %d %s", metric + 1,
                         unavailable ? "N/A: " : "exceeds range") ||
                (unavailable &&
                 !append_html_text(buffer, items[i].v2_reason[metric])) ||
                !append(buffer, "</li>")) return FALSE;
        }
        if (items[i].rank_tied) {
            if (!opened) {
                if (!append(buffer, "<section class=\"item\"><h2>Warnings"
                            "</h2><ul>")) return FALSE;
                opened = TRUE;
            }
            if (!append(buffer, "<li>") ||
                !append_html_text(buffer, items[i].image_name) ||
                !append(buffer, ": ranking tied within JND (&asymp;).</li>"))
                return FALSE;
        }
    }
    return !opened || append(buffer, "</ul></section>");
}

static BOOL append_csv_link(report_buffer_t *html,
                            const metrics_item_result_t *items, int count)
{
    static const char header[] =
        "image,rank,score,S1_edge_gain,N1_noise_sigma,N1_snr_db,"
        "C1_chroma_sigma,C3_gray_cast,K1_chroma_mean,K1_chroma_p95\r\n";
    report_buffer_t csv = { NULL, 0, 0, FALSE };
    char *encoded, *utf8;
    size_t encoded_length;
    int i;
    append(&csv, header);
    for (i = 0; i < count; i++) {
        const char *name = items[i].image_name;
        append(&csv, "\"");
        while (*name) {
            if (*name == '"') append(&csv, "\"\"");
            else append_n(&csv, name, 1);
            name++;
        }
        appendf(&csv, "\",%d,%.4f", items[i].rank_order,
                items[i].rank_score);
        appendf(&csv, ",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\r\n",
                items[i].v2[METRIC_S1_EDGE_GAIN],
                items[i].v2[METRIC_N1_NOISE_SIGMA],
                items[i].v2[METRIC_N1_SNR_DB],
                items[i].v2[METRIC_C1_CHROMA_SIGMA],
                items[i].v2[METRIC_C3_GRAY_CAST],
                items[i].v2[METRIC_K1_CHROMA_MEAN],
                items[i].v2[METRIC_K1_CHROMA_P95]);
    }
    if (csv.failed) { free(csv.data); return FALSE; }
    utf8 = Report_AnsiToUtf8(csv.data);
    free(csv.data);
    if (!utf8) return FALSE;
    encoded = Report_Base64Encode((const BYTE *)utf8, strlen(utf8),
                                  &encoded_length);
    free(utf8);
    if (!encoded) return FALSE;
    append(html, "<p><a download=\"roi-metrics.csv\" href=\"data:text/csv;"
           "base64,");
    append_n(html, encoded, encoded_length);
    append(html, "\">Download Metric Set v2 CSV</a></p>");
    free(encoded);
    return !html->failed;
}

static BOOL append_item(report_buffer_t *buffer,
                        const metrics_item_result_t *item, int index)
{
    const metrics_stage1_t *s1 = &item->s1;
    BOOL ok;
    ok = appendf(buffer, "<section class=\"item\"><h2>%d. ", index + 1) &&
         append_html_text(buffer, item->image_name) &&
         appendf(buffer, "</h2><p>ROI [%ld, %ld] - [%ld, %ld], %ld x %ld; "
                  "source %d x %d</p><details>"
                  "<summary>Legacy metrics</summary><table>"
                  "<tr><th>Metric</th><th>Value</th></tr>",
                  (long)item->source_rect.left, (long)item->source_rect.top,
                  (long)item->source_rect.right, (long)item->source_rect.bottom,
                  (long)(item->source_rect.right - item->source_rect.left),
                  (long)(item->source_rect.bottom - item->source_rect.top),
                  item->image_w, item->image_h);
    if (!ok)
        return FALSE;
#define METRIC_ROW(label, value, format) \
    do { if (!append_row(buffer, label, value, format)) return FALSE; } while (0)
    METRIC_ROW("Sharpness score", s1->quality_score, "%.2f / 100");
    METRIC_ROW("Laplacian variance", s1->laplacian_var, "%.4f");
    METRIC_ROW("Sobel mean", s1->sobel_mean, "%.4f");
    METRIC_ROW("Tenengrad", s1->tenengrad, "%.4f");
    METRIC_ROW("Brenner", s1->brenner, "%.4f");
    METRIC_ROW("Edge density", s1->edge_density, "%.3f%%");
    METRIC_ROW("8-direction contrast", s1->multi_dir_contrast, "%.4f");
    METRIC_ROW("Cumulative contrast", s1->cumulative_contrast, "%.4f");
    METRIC_ROW("Noise estimate", s1->noise_estimate, "%.4f");
    METRIC_ROW("SNR", s1->snr_db, "%.2f dB");
    METRIC_ROW("Shadow energy", s1->zone_contrast[METRICS_ZONE_SHADOW], "%.4f");
    METRIC_ROW("Low-tone energy", s1->zone_contrast[METRICS_ZONE_LOW], "%.4f");
    METRIC_ROW("Mid-tone energy", s1->zone_contrast[METRICS_ZONE_MID], "%.4f");
    METRIC_ROW("Highlight energy", s1->zone_contrast[METRICS_ZONE_HIGH], "%.4f");
    METRIC_ROW("Zone energy range", s1->zone_mean_range, "%.4f");
    METRIC_ROW("Saturation mean", s1->sat_mean, "%.4f");
    METRIC_ROW("Saturation deviation", s1->sat_std, "%.4f");
    METRIC_ROW("Low-saturation pixels", s1->sat_low_ratio, "%.2f%%");
    METRIC_ROW("High-saturation pixels", s1->sat_high_ratio, "%.2f%%");
    if (item->has_stage2) {
        const metrics_stage2_t *s2 = &item->s2;
        METRIC_ROW("FFT low frequency", s2->fft_low_energy, "%.4e");
        METRIC_ROW("FFT mid frequency", s2->fft_mid_energy, "%.4e");
        METRIC_ROW("FFT high frequency", s2->fft_high_energy, "%.4e");
        METRIC_ROW("FFT high-frequency ratio", s2->fft_high_ratio, "%.2f%%");
        METRIC_ROW("Gray-cast Delta E", s2->gray_cast_delta_e, "%.3f");
        if (!appendf(buffer, "<tr><th>Gray cast</th><td>%s "
                     "(a*: %.3f, b*: %.3f)</td></tr>"
                     "<tr><th>Shadow defect</th><td>%s "
                     "(a*: %.3f, b*: %.3f)</td></tr>",
                     s2->gray_cast_text, s2->gray_cast_lab_a,
                     s2->gray_cast_lab_b, s2->shadow_defect,
                     s2->shadow_lab_a, s2->shadow_lab_b))
            return FALSE;
        METRIC_ROW("Mean Lab L*", s2->mean_lab_l, "%.3f");
        METRIC_ROW("Mean Lab a*", s2->mean_lab_a, "%.3f");
        METRIC_ROW("Mean Lab b*", s2->mean_lab_b, "%.3f");
        if (s2->edge_png_base64 &&
            (!append(buffer, "</table><img class=\"edge\" alt=\"Laplacian "
                     "edge preview\" src=\"data:image/png;base64,") ||
             !append_n(buffer, s2->edge_png_base64,
                       s2->edge_png_base64_len) ||
             !append(buffer, "\"><table>")))
            return FALSE;
    }
#undef METRIC_ROW
    return append(buffer, "</table></details>") &&
           append_v2_details(buffer, item) &&
           append(buffer, "</section>");
}

BOOL Report_WriteHtmlFile(const metrics_item_result_t *items, int count,
                          const char *title, const char *file_path)
{
    static const char prefix[] =
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>";
    static const char style[] =
        "</title><style>body{font:15px Segoe UI,Arial,sans-serif;margin:2rem;"
        "background:#101820;color:#e9f0f5}.item{background:#1b2833;"
        "padding:1rem 1.5rem;margin:1.2rem 0;border-radius:8px}h1,h2{color:#72c7e8}"
        "table{border-collapse:collapse;width:100%;margin:1rem 0}td,th{"
        "border-bottom:1px solid #43535e;padding:.45rem;text-align:left}"
        "th{width:48%;color:#b9cad5}summary{cursor:pointer;color:#72c7e8}"
        ".edge{max-width:512px;width:100%;"
        "image-rendering:pixelated}canvas{width:100%;max-width:850px;height:300px}"
        "</style></head><body><h1>";
    static const char chart[] =
        "</h1><canvas id=\"scores\" width=\"850\" height=\"300\"></canvas>"
        "<script>(function(){var c=document.getElementById('scores'),"
        "x=c.getContext('2d'),v=[";
    static const char chart_end[] =
        "],w=c.width/(v.length||1);x.fillStyle='#72c7e8';"
        "for(var i=0;i<v.length;i++){var h=Math.max(0,Math.min(100,v[i]))*2.5;"
        "x.fillRect(i*w+8,c.height-h-24,Math.max(2,w-16),h);"
        "x.fillStyle='#e9f0f5';x.fillText((i+1)+': '+v[i].toFixed(1),"
        "i*w+8,c.height-6);x.fillStyle='#72c7e8';}})();</script>";
    report_buffer_t html = { NULL, 0, 0, FALSE };
    HANDLE file = INVALID_HANDLE_VALUE;
    BOOL success = FALSE;
    int i;
    if (!items || count <= 0 || count > CMP_MAX_CELLS ||
        !title || !file_path || !file_path[0])
        return FALSE;
    append(&html, prefix);
    append_html_text(&html, title);
    append(&html, style);
    append_html_text(&html, title);
    append(&html, chart);
    for (i = 0; i < count; i++) {
        if (i)
            append(&html, ",");
        appendf(&html, "%.4f", items[i].s1.quality_score);
    }
    append(&html, chart_end);
    append_rank_summary(&html, items, count);
    append_warnings(&html, items, count);
    append_csv_link(&html, items, count);
    if (count > 1 && items[0].has_stage2) {
        append(&html, "<section class=\"item\"><h2>Mean-color Delta E76 "
                 "from item 1</h2><table><tr><th>Image</th>"
                 "<th>Delta E76</th></tr>");
        for (i = 1; i < count; i++) {
            double dl, da, db, delta;
            if (!items[i].has_stage2)
                continue;
            dl = items[i].s2.mean_lab_l - items[0].s2.mean_lab_l;
            da = items[i].s2.mean_lab_a - items[0].s2.mean_lab_a;
            db = items[i].s2.mean_lab_b - items[0].s2.mean_lab_b;
            delta = sqrt(dl * dl + da * da + db * db);
            append(&html, "<tr><td>");
            append_html_text(&html, items[i].image_name);
            appendf(&html, "</td><td>%.4f</td></tr>", delta);
        }
        append(&html, "</table></section>");
    }
    for (i = 0; i < count; i++)
        append_item(&html, &items[i], i);
    append(&html, "</body></html>");
    if (html.failed)
        goto done;
    file = CreateFileA(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        goto done;
    {
        static const BYTE bom[] = { 0xef, 0xbb, 0xbf };
        DWORD written;
        size_t offset = 0;
        if (!WriteFile(file, bom, (DWORD)sizeof(bom), &written, NULL) ||
            written != sizeof(bom))
            goto done;
        while (offset < html.length) {
            DWORD chunk = (DWORD)(((html.length - offset) > MAXDWORD) ?
                                  MAXDWORD : html.length - offset);
            if (!WriteFile(file, html.data + offset, chunk, &written, NULL) ||
                written != chunk)
                goto done;
            offset += written;
        }
    }
    success = TRUE;
done:
    if (file != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(file))
            success = FALSE;
    }
    free(html.data);
    return success;
}

BOOL Report_GenerateAndOpen(const metrics_item_result_t *items, int count,
                            const char *title, const char *out_html_path)
{
    char path[MAX_PATH];
    char tmp_file[MAX_PATH];
    BOOL generated = FALSE;
    HINSTANCE result;
    if (out_html_path && out_html_path[0]) {
        if (strlen(out_html_path) >= sizeof(path)) {
            MessageBoxA(NULL, "The report path is too long.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        lstrcpynA(path, out_html_path, (int)sizeof(path));
        generated = Report_WriteHtmlFile(items, count, title, path);
    } else {
        DWORD length = GetTempPathA((DWORD)sizeof(path), path);
        if (!length || length >= sizeof(path) ||
            !GetTempFileNameA(path, "roi", 0, tmp_file)) {
            MessageBoxA(NULL, "Could not create a temporary report path.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        lstrcpynA(path, tmp_file, (int)sizeof(path));
        if (!PathRenameExtensionA(path, ".html")) {
            DeleteFileA(tmp_file);
            MessageBoxA(NULL, "Could not create a temporary report path.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        if (!MoveFileA(tmp_file, path)) {
            DeleteFileA(tmp_file);
            MessageBoxA(NULL, "Could not reserve a temporary report file.",
                        "Metrics Report", MB_OK | MB_ICONERROR);
            return FALSE;
        }
        generated = Report_WriteHtmlFile(items, count, title, path);
    }
    if (!generated) {
        if (!out_html_path || !out_html_path[0])
            DeleteFileA(path);
        MessageBoxA(NULL, "Could not write the HTML metrics report.",
                    "Metrics Report", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    result = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        char message[MAX_PATH + 96];
        _snprintf(message, sizeof(message),
                  "The HTML report was created, but the browser could not be "
                  "opened:\n%s", path);
        message[sizeof(message) - 1] = '\0';
        MessageBoxA(NULL, message, "Metrics Report",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        return FALSE;
    }
    return TRUE;
}
