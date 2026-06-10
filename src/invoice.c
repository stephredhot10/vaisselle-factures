#include "app.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char export_error[512];

const char *invoice_export_error_message(void) {
    return export_error[0] ? export_error : "Export impossible";
}

static void set_export_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(export_error, sizeof(export_error), fmt, args);
    va_end(args);
}

static char *esc(const unsigned char *s) {
    if (!s) return g_strdup("");
    return g_markup_escape_text((const char*)s, -1);
}

static char *fmt_amount(double amount, gboolean with_euro) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", amount);
    for (char *p = buf; *p; p++) {
        if (*p == '.') {
            *p = ',';
            break;
        }
    }
    return with_euro ? g_strdup_printf("%s €", buf) : g_strdup(buf);
}

char *invoice_export_html(App *app, int invoice_id) {
    export_error[0] = '\0';
    sqlite3_stmt *st;
    const char *sql = "SELECT i.number,i.date,i.total_ht,i.total_tva,i.total_ttc,c.name,c.address,c.email,c.phone,co.name,co.siret,co.address,co.email,co.phone,co.legal FROM invoices i JOIN clients c ON c.id=i.client_id JOIN company co ON co.id=1 WHERE i.id=?";
    if (sqlite3_prepare_v2(app->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_export_error("Impossible de préparer la requête facture");
        return NULL;
    }
    sqlite3_bind_int(st,1,invoice_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        set_export_error("Facture introuvable");
        return NULL;
    }
    char *num=esc(sqlite3_column_text(st,0)), *date=esc(sqlite3_column_text(st,1));
    double total_ht=sqlite3_column_double(st,2), total_tva=sqlite3_column_double(st,3), total_ttc=sqlite3_column_double(st,4);
    char *cname=esc(sqlite3_column_text(st,5)), *caddr=esc(sqlite3_column_text(st,6)), *cemail=esc(sqlite3_column_text(st,7)), *cphone=esc(sqlite3_column_text(st,8));
    char *oname=esc(sqlite3_column_text(st,9)), *osiret=esc(sqlite3_column_text(st,10)), *oaddr=esc(sqlite3_column_text(st,11)), *oemail=esc(sqlite3_column_text(st,12)), *ophone=esc(sqlite3_column_text(st,13)), *olegal=esc(sqlite3_column_text(st,14));
    sqlite3_finalize(st);

    char *total_ht_s = fmt_amount(total_ht, TRUE);
    char *total_tva_s = fmt_amount(total_tva, TRUE);
    char *total_ttc_s = fmt_amount(total_ttc, TRUE);

    GString *html = g_string_new(NULL);
    g_string_append_printf(html, "<!doctype html><html><head><meta charset='utf-8'><title>%s</title><style>body{font-family:Arial,sans-serif;margin:32px;color:#222}.top{display:flex;justify-content:space-between;gap:40px}.box{border:1px solid #ddd;padding:14px;border-radius:8px}h1{color:#385f46}table{width:100%%;border-collapse:collapse;margin-top:26px}th,td{border-bottom:1px solid #ddd;padding:9px;text-align:left}th{background:#eef5ef}.right{text-align:right}.totals{margin-left:auto;width:320px}.muted{color:#666;white-space:pre-line}@media print{button{display:none}body{margin:10mm}}</style></head><body>", num);
    g_string_append_printf(html, "<button onclick='window.print()'>Imprimer / Enregistrer en PDF</button><div class='top'><div><h1>Facture %s</h1><p>Date : %s</p></div><div class='box'><strong>%s</strong><br>SIRET : %s<br><span class='muted'>%s</span><br>%s<br>%s</div></div>", num,date,oname,osiret,oaddr,oemail,ophone);
    g_string_append_printf(html, "<h2>Client</h2><div class='box'><strong>%s</strong><br><span class='muted'>%s</span><br>%s<br>%s</div>", cname,caddr,cemail,cphone);
    g_string_append(html, "<table><thead><tr><th>Désignation</th><th class='right'>Qté</th><th class='right'>PU HT</th><th class='right'>TVA</th><th class='right'>Total TTC</th></tr></thead><tbody>");
    sqlite3_stmt *ls;
    if (sqlite3_prepare_v2(app->db, "SELECT description,quantity,unit_price,vat_rate,line_ttc FROM invoice_lines WHERE invoice_id=? ORDER BY id", -1, &ls, NULL) == SQLITE_OK) {
        sqlite3_bind_int(ls,1,invoice_id);
        while (sqlite3_step(ls) == SQLITE_ROW) {
            char *d=esc(sqlite3_column_text(ls,0));
            char *qty_s = fmt_amount(sqlite3_column_double(ls,1), FALSE);
            char *price_s = fmt_amount(sqlite3_column_double(ls,2), TRUE);
            char *vat_s = fmt_amount(sqlite3_column_double(ls,3), FALSE);
            char *ttc_s = fmt_amount(sqlite3_column_double(ls,4), TRUE);
            g_string_append_printf(html, "<tr><td>%s</td><td class='right'>%s</td><td class='right'>%s</td><td class='right'>%s %%</td><td class='right'>%s</td></tr>", d, qty_s, price_s, vat_s, ttc_s);
            g_free(d);
            g_free(qty_s);
            g_free(price_s);
            g_free(vat_s);
            g_free(ttc_s);
        }
        sqlite3_finalize(ls);
    }
    g_string_append_printf(html, "</tbody></table><table class='totals'><tr><td>Total HT</td><td class='right'>%s</td></tr><tr><td>TVA</td><td class='right'>%s</td></tr><tr><th>Total TTC</th><th class='right'>%s</th></tr></table><p class='muted'>%s</p></body></html>", total_ht_s, total_tva_s, total_ttc_s, olegal);

    char *dir = g_build_filename(g_get_home_dir(), "FacturesVaisselle", NULL);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        set_export_error("Impossible de créer le dossier %s", dir);
        g_string_free(html, TRUE);
        g_free(num);g_free(date);g_free(cname);g_free(caddr);g_free(cemail);g_free(cphone);
        g_free(oname);g_free(osiret);g_free(oaddr);g_free(oemail);g_free(ophone);g_free(olegal);
        g_free(total_ht_s);g_free(total_tva_s);g_free(total_ttc_s);
        g_free(dir);
        return NULL;
    }
    char *safe = g_strdup(num);
    for (char *p=safe; *p; p++) {
        if (*p=='/' || *p==' ') *p='-';
    }
    char *path = g_strdup_printf("%s/%s.html", dir, safe);
    FILE *f = fopen(path, "w");
    if (!f) {
        set_export_error("Impossible d'écrire %s : %s", path, strerror(errno));
        g_free(path);
        path = NULL;
    } else {
        fputs(html->str, f);
        fclose(f);
    }
    g_string_free(html, TRUE);
    g_free(num);g_free(date);g_free(cname);g_free(caddr);g_free(cemail);g_free(cphone);
    g_free(oname);g_free(osiret);g_free(oaddr);g_free(oemail);g_free(ophone);g_free(olegal);
    g_free(total_ht_s);g_free(total_tva_s);g_free(total_ttc_s);
    g_free(dir);g_free(safe);
    return path;
}