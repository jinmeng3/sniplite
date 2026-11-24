/* Text that stands in columns, read as a table.
 *
 * The recognizer gives lines of text and where they are. Two things make a table of
 * them, and neither needs a model:
 *
 * Columns. A column boundary is a vertical strip that no line of any row with several
 * lines crosses. One sweep over the lines, sorted by their left edge, finds every such
 * strip. A strip counts when at least two rows have text on both sides of it, and when
 * it is wide enough for how many rows vouch for it (a narrow strip between two rows is
 * chance; between four it is a column).
 *
 * Rows. Lines side by side are one row. Where the picture has a rule under nearly
 * every row (a grid, or striped rows), the rules are the row boundaries instead, so a
 * cell whose text wraps over two or three lines stays one cell. A rule is a long run
 * of pixels that differ from the pixel above them.
 *
 * The result is the cells of each row separated by tabs: what a spreadsheet takes
 * from the clipboard. Text without columns is not a table, and is left alone. */
#pragma GCC optimize("O2")
#include "ocr.h"
#include <emmintrin.h>

#define MAX_COLS 64
#define MAX_RULES 2048

typedef struct {
    int x0, x1;
} Span;

static void sort_spans(Span *a, int n)
{
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            Span v = a[i];
            int j = i;
            for (; j >= gap && a[j - gap].x0 > v.x0; j -= gap)
                a[j] = a[j - gap];
            a[j] = v;
        }
}

/* the usual height of a line */
static int usual_height(const OcrOut *o)
{
    int hist[256] = {0}, half = (o->n + 1) / 2, seen = 0;
    for (int i = 0; i < o->n; i++)
        hist[CLAMPI(o->line[i].rc.bottom - o->line[i].rc.top, 0, 255)]++;
    for (int h = 0; h < 256; h++)
        if ((seen += hist[h]) >= half)
            return MAXI(h, 1);
    return 1;
}

/* the middles of the strips between columns, left to right */
static int find_cuts(const OcrOut *o, int *cut)
{
    int n = o->n, ns = 0, ncut = 0, tall = usual_height(o);
    Span *sp = mem_alloc((size_t)n * sizeof(Span));
    if (!sp)
        return 0;
    for (int i = 0; i < n;) { /* the lines of rows that have several */
        int j = i + 1;
        while (j < n && o->line[j].row == o->line[i].row)
            j++;
        if (j - i > 1)
            for (int k = i; k < j; k++)
                sp[ns++] = (Span){o->line[k].rc.left, o->line[k].rc.right};
        i = j;
    }
    sort_spans(sp, ns);
    for (int i = 1, reach = ns ? sp[0].x1 : 0; i < ns && ncut < MAX_COLS - 1; i++) {
        int g0 = reach, g1 = sp[i].x0;
        reach = MAXI(reach, sp[i].x1);
        if (g1 <= g0)
            continue;
        int vouch = 0; /* rows with text on both sides */
        for (int a = 0; a < n;) {
            int b = a, left = 0, right = 0;
            for (; b < n && o->line[b].row == o->line[a].row; b++) {
                left |= o->line[b].rc.right <= g0;
                right |= o->line[b].rc.left >= g1;
            }
            vouch += left & right;
            a = b;
        }
        if (vouch >= 2 && (g1 - g0) * 10 >= tall * (vouch >= 4 ? 3 : 10))
            cut[ncut++] = (g0 + g1) / 2;
    }
    mem_free(sp);
    return ncut;
}

/* The rows of pixels that are a rule: within [x0, x1), at least `need` pixels differ
 * from the one above (a count, not a run: the vertical lines of a grid cross it).
 * Neighbouring rows count once. */
static int find_rules(const uint32_t *px, int stride, int h, int x0, int x1, int need, int *rule)
{
    static const uint8_t ones[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
    const __m128i rgb = _mm_set1_epi32(0x00FFFFFF), tol = _mm_set1_epi8(12), zero = _mm_setzero_si128();
    int nrule = 0;
    for (int y = 1; y < h && nrule < MAX_RULES; y++) {
        const uint32_t *a = px + (size_t)y * stride, *b = a - stride;
        int same = 0, x = x0, n = (x1 - x0) & ~3;
        for (; x + 4 <= x1; x += 4) {
            __m128i p = _mm_loadu_si128((const __m128i *)(a + x)), q = _mm_loadu_si128((const __m128i *)(b + x));
            __m128i d = _mm_and_si128(_mm_or_si128(_mm_subs_epu8(p, q), _mm_subs_epu8(q, p)), rgb);
            same += ones[_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(_mm_subs_epu8(d, tol), zero)))];
        }
        if (n - same < need)
            continue;
        if (nrule && y - rule[nrule - 1] <= 3)
            rule[nrule - 1] = y; /* the lower edge of the same rule */
        else
            rule[nrule++] = y;
    }
    return nrule;
}

static bool wide_char(WCHAR c) { return c >= 0x2E80; } /* written without spaces between words */

bool ocr_table(const OcrOut *o, const uint32_t *px, int stride, int w, int h, Str *s)
{
    int n = o->n, cut[MAX_COLS];
    if (n < 4)
        return false;
    int ncut = find_cuts(o, cut);
    if (!ncut)
        return false;
    int rows = o->line[n - 1].row + 1;
    int *trow = mem_alloc((size_t)rows * sizeof(int)), *rule = mem_alloc(MAX_RULES * sizeof(int));
    Str cell[MAX_COLS];
    memset(cell, 0, sizeof cell);
    bool table = false;
    if (!trow || !rule)
        goto done;

    /* the rows: the rules of a grid where there is one, the rows of text otherwise */
    int x0 = w, x1 = 0;
    for (int i = 0; i < n; i++) {
        x0 = MINI(x0, o->line[i].rc.left);
        x1 = MAXI(x1, o->line[i].rc.right);
    }
    x0 = CLAMPI(x0, 0, w), x1 = CLAMPI(x1, x0, w);
    int need = MAXI((x1 - x0) * 6 / 10, 32);
    int nrule = px ? find_rules(px, stride, h, x0, x1, need, rule) : 0;
    /* A band between two rules is one row of a grid. Above the first rule and below
     * the last there is no telling (a title, a table with one rule under its head). */
    int closed = 0, inside = 0, last = -1;
    for (int i = 0; i < n;) {
        int j = i, top = h, bottom = 0, r = o->line[i].row, band = 0;
        for (; j < n && o->line[j].row == r; j++) {
            top = MINI(top, o->line[j].rc.top);
            bottom = MAXI(bottom, o->line[j].rc.bottom);
        }
        while (band < nrule && rule[band] * 2 < top + bottom)
            band++;
        trow[r] = band > 0 && band < nrule ? band : -1;
        if (trow[r] >= 0) {
            inside++;
            closed += band != last;
            last = band;
        }
        i = j;
    }
    bool grid = closed >= 3 && closed * 2 >= inside;
    for (int r = 0, t = -1, band = -1; r < rows; r++) { /* number the table's rows */
        if (!grid || trow[r] < 0 || trow[r] != band)
            t++;
        band = grid ? trow[r] : -1;
        trow[r] = t;
    }

    /* the cells, row by row */
    int full = 0, total = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || (i && trow[o->line[i].row] != trow[o->line[i - 1].row])) {
            int used = 0, lastcol = -1;
            for (int c = 0; c <= ncut; c++)
                if (cell[c].n) {
                    used++;
                    lastcol = c;
                }
            if (total)
                str_addn(s, L"\r\n", 2);
            for (int c = 0; c <= lastcol; c++) {
                if (c)
                    str_addn(s, L"\t", 1);
                if (cell[c].n)
                    str_addn(s, cell[c].p, cell[c].n);
                cell[c].n = 0;
            }
            full += used >= 2;
            total++;
            if (i == n)
                break;
        }
        const OcrLine *ln = &o->line[i];
        if (!ln->len)
            continue;
        int col = 0;
        while (col < ncut && cut[col] <= ln->rc.left)
            col++;
        Str *c = &cell[col];
        const WCHAR *t = o->chars.p + ln->text;
        if (c->n && !(wide_char(c->p[c->n - 1]) && wide_char(t[0])))
            str_addn(c, L" ", 1);
        str_addn(c, t, ln->len);
    }
    table = full >= 2 && full * 5 >= total * 2;
done:
    for (int c = 0; c < MAX_COLS; c++)
        str_free(&cell[c]);
    mem_free(trow);
    mem_free(rule);
    if (!table && s->p) {
        s->n = 0;
        s->p[0] = 0;
    }
    return table;
}
