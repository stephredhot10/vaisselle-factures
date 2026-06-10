#include "app.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *app_data_path(const char *name) {
    const char *base = g_get_user_data_dir();
    char *dir = g_build_filename(base, "vaisselle-factures", NULL);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return NULL;
    }
    char *path = g_build_filename(dir, name, NULL);
    g_free(dir);
    return path;
}

static char *text_view_dup(GtkWidget *view) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(b, &start, &end);
    return gtk_text_buffer_get_text(b, &start, &end, FALSE);
}

static const char *safe_text(const unsigned char *text) {
    return text ? (const char *)text : "";
}

static int db_exec(sqlite3 *db, const char *sql) {
    return sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK;
}

static double round_money(double v) {
    long long cents = (long long)(v * 100.0 + (v >= 0 ? 0.5 : -0.5));
    return cents / 100.0;
}

static int valid_invoice_status(const char *status) {
    return status && (
        strcmp(status, "brouillon") == 0 ||
        strcmp(status, "envoyée") == 0 ||
        strcmp(status, "payée") == 0
    );
}

int db_open(App *app) {
    char *path = app_data_path("vaisselle_factures.db");
    if (!path) return SQLITE_CANTOPEN;
    sqlite3_config(SQLITE_CONFIG_SERIALIZED);
    int rc = sqlite3_open(path, &app->db);
    g_free(path);
    if (rc != SQLITE_OK) return rc;
    sqlite3_exec(app->db, "PRAGMA foreign_keys=ON", NULL, NULL, NULL);
    return db_init(app->db);
}

void db_close(App *app) {
    if (app->db) sqlite3_close(app->db);
}

int db_init(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS company(id INTEGER PRIMARY KEY CHECK(id=1), name TEXT, siret TEXT, address TEXT, email TEXT, phone TEXT, legal TEXT);"
        "INSERT OR IGNORE INTO company(id,name,siret,address,email,phone,legal) VALUES(1,'','','','','','');"
        "UPDATE company SET legal='' WHERE id=1 AND legal='TVA non applicable, art. ' || '293 B du CGI';"
        "CREATE TABLE IF NOT EXISTS clients(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, address TEXT, email TEXT, phone TEXT, created_at TEXT DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE IF NOT EXISTS invoices(id INTEGER PRIMARY KEY AUTOINCREMENT, number TEXT UNIQUE, client_id INTEGER NOT NULL REFERENCES clients(id), date TEXT DEFAULT CURRENT_DATE, status TEXT DEFAULT 'brouillon', total_ht REAL DEFAULT 0, total_tva REAL DEFAULT 0, total_ttc REAL DEFAULT 0);"
        "CREATE TABLE IF NOT EXISTS invoice_lines(id INTEGER PRIMARY KEY AUTOINCREMENT, invoice_id INTEGER NOT NULL REFERENCES invoices(id) ON DELETE CASCADE, description TEXT NOT NULL, quantity REAL NOT NULL, unit_price REAL NOT NULL, vat_rate REAL NOT NULL, line_ht REAL NOT NULL, line_tva REAL NOT NULL, line_ttc REAL NOT NULL);";
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "SQL error during init: %s\n", err);
            sqlite3_free(err);
        }
        return rc;
    }
    return SQLITE_OK;
}

int db_save_company(App *app) {
    sqlite3_stmt *st;
    const char *sql = "UPDATE company SET name=?, siret=?, address=?, email=?, phone=?, legal=? WHERE id=1";
    if (sqlite3_prepare_v2(app->db, sql, -1, &st, NULL) != SQLITE_OK) return SQLITE_ERROR;

    char *address = text_view_dup(app->company_address);
    char *legal = text_view_dup(app->company_legal);
    int rc = SQLITE_ERROR;

    sqlite3_bind_text(st, 1, gtk_entry_get_text(GTK_ENTRY(app->company_name)), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, gtk_entry_get_text(GTK_ENTRY(app->company_siret)), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, address, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, gtk_entry_get_text(GTK_ENTRY(app->company_email)), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, gtk_entry_get_text(GTK_ENTRY(app->company_phone)), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, legal, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) == SQLITE_DONE) {
        rc = SQLITE_OK;
    }
    sqlite3_finalize(st);
    g_free(address);
    g_free(legal);
    return rc;
}

int db_load_company(App *app) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(app->db, "SELECT name,siret,address,email,phone,legal FROM company WHERE id=1", -1, &st, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    if (sqlite3_step(st) == SQLITE_ROW) {
        gtk_entry_set_text(GTK_ENTRY(app->company_name), safe_text(sqlite3_column_text(st,0)));
        gtk_entry_set_text(GTK_ENTRY(app->company_siret), safe_text(sqlite3_column_text(st,1)));
        gtk_entry_set_text(GTK_ENTRY(app->company_email), safe_text(sqlite3_column_text(st,3)));
        gtk_entry_set_text(GTK_ENTRY(app->company_phone), safe_text(sqlite3_column_text(st,4)));
        GtkTextBuffer *a = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->company_address));
        GtkTextBuffer *l = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->company_legal));
        gtk_text_buffer_set_text(a, safe_text(sqlite3_column_text(st,2)), -1);
        gtk_text_buffer_set_text(l, safe_text(sqlite3_column_text(st,5)), -1);
    }
    sqlite3_finalize(st);
    return SQLITE_OK;
}

int db_add_client(App *app) {
    const char *name = gtk_entry_get_text(GTK_ENTRY(app->client_name));
    if (!name || strlen(name) == 0 || strlen(name) >= 255) {
        return SQLITE_MISUSE;
    }

    sqlite3_stmt *st;
    const char *sql = "INSERT INTO clients(name,address,email,phone) VALUES(?,?,?,?)";
    if (sqlite3_prepare_v2(app->db, sql, -1, &st, NULL) != SQLITE_OK) return SQLITE_ERROR;

    char *address = text_view_dup(app->client_address);
    const char *email = gtk_entry_get_text(GTK_ENTRY(app->client_email));
    const char *phone = gtk_entry_get_text(GTK_ENTRY(app->client_phone));

    sqlite3_bind_text(st,1,name,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,2,address,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,3,email,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(st,4,phone,-1,SQLITE_TRANSIENT);

    int rc = sqlite3_step(st) == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
    sqlite3_finalize(st);
    g_free(address);
    return rc;
}

int db_load_clients(App *app) {
    gtk_list_store_clear(app->clients_store);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(app->db, "SELECT id,name FROM clients ORDER BY name", -1, &st, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        GtkTreeIter it;
        gtk_list_store_append(app->clients_store, &it);
        gtk_list_store_set(app->clients_store, &it, 0, sqlite3_column_int(st,0), 1, sqlite3_column_text(st,1), -1);
    }
    sqlite3_finalize(st);
    return SQLITE_OK;
}

static int invoice_number_exists(sqlite3 *db, const char *number) {
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM invoices WHERE number=?", -1, &st, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(st, 1, number, -1, SQLITE_TRANSIENT);
    int exists = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return exists ? 1 : 0;
}

static char *next_invoice_number(sqlite3 *db) {
    sqlite3_stmt *st;
    int seq = 1;

    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(id),0)+1 FROM invoices", -1, &st, NULL) != SQLITE_OK) {
        return NULL;
    }
    if (sqlite3_step(st) == SQLITE_ROW) {
        seq = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);

    GDateTime *now = g_date_time_new_now_local();
    int year = g_date_time_get_year(now);
    g_date_time_unref(now);

    for (int attempt = 0; attempt < 100; attempt++) {
        char *num = g_strdup_printf("FAC-%04d-%04d", year, seq);
        int check = invoice_number_exists(db, num);
        if (check == 0) {
            return num;
        }
        g_free(num);
        if (check < 0) {
            return NULL;
        }
        seq++;
    }

    return NULL;
}

int db_create_invoice(App *app, int client_id) {
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app->lines_store), &iter);
    if (!valid) return SQLITE_MISUSE;

    if (!db_exec(app->db, "BEGIN")) return SQLITE_ERROR;
    char *number = next_invoice_number(app->db);
    if (!number) {
        db_exec(app->db, "ROLLBACK");
        return SQLITE_ERROR;
    }
    sqlite3_stmt *inv;

    if (sqlite3_prepare_v2(app->db, "INSERT INTO invoices(number,client_id) VALUES(?,?)", -1, &inv, NULL) != SQLITE_OK) {
        db_exec(app->db, "ROLLBACK");
        g_free(number);
        return SQLITE_ERROR;
    }

    sqlite3_bind_text(inv,1,number,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(inv,2,client_id);
    int ok = sqlite3_step(inv) == SQLITE_DONE;
    sqlite3_finalize(inv);

    int invoice_id = (int)sqlite3_last_insert_rowid(app->db);
    double total_ht=0,total_tva=0,total_ttc=0;

    while (valid && ok) {
        gchar *desc=NULL; double qty=0, price=0, vat=0;
        gtk_tree_model_get(GTK_TREE_MODEL(app->lines_store), &iter, 0, &desc, 1, &qty, 2, &price, 3, &vat, -1);

        if (price <= 0 || qty <= 0 || vat < 0) {
            ok = 0;
            g_free(desc);
            break;
        }

        double ht = round_money(qty * price);
        double tva = round_money(ht * vat / 100.0);
        double ttc = round_money(ht + tva);
        sqlite3_stmt *line;
        ok = sqlite3_prepare_v2(app->db, "INSERT INTO invoice_lines(invoice_id,description,quantity,unit_price,vat_rate,line_ht,line_tva,line_ttc) VALUES(?,?,?,?,?,?,?,?)", -1, &line, NULL) == SQLITE_OK;

        if (ok) {
            sqlite3_bind_int(line,1,invoice_id);
            sqlite3_bind_text(line,2,desc,-1,SQLITE_TRANSIENT);
            sqlite3_bind_double(line,3,qty);
            sqlite3_bind_double(line,4,price);
            sqlite3_bind_double(line,5,vat);
            sqlite3_bind_double(line,6,ht);
            sqlite3_bind_double(line,7,tva);
            sqlite3_bind_double(line,8,ttc);
            ok = sqlite3_step(line) == SQLITE_DONE;
            sqlite3_finalize(line);
        }

        total_ht = round_money(total_ht + ht);
        total_tva = round_money(total_tva + tva);
        total_ttc = round_money(total_ttc + ttc);
        g_free(desc);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(app->lines_store), &iter);
    }

    if (ok) {
        sqlite3_stmt *up;
        ok = sqlite3_prepare_v2(app->db, "UPDATE invoices SET total_ht=?, total_tva=?, total_ttc=? WHERE id=?", -1, &up, NULL) == SQLITE_OK;
        if (ok) {
            sqlite3_bind_double(up,1,total_ht);
            sqlite3_bind_double(up,2,total_tva);
            sqlite3_bind_double(up,3,total_ttc);
            sqlite3_bind_int(up,4,invoice_id);
            ok = sqlite3_step(up) == SQLITE_DONE;
            sqlite3_finalize(up);
        }
    }

    if (ok) {
        if (!db_exec(app->db, "COMMIT")) {
            db_exec(app->db, "ROLLBACK");
            g_free(number);
            return SQLITE_ERROR;
        }
    } else {
        db_exec(app->db, "ROLLBACK");
    }
    g_free(number);
    return ok ? SQLITE_OK : SQLITE_ERROR;
}

int db_load_invoices(App *app) {
    gtk_list_store_clear(app->invoices_store);
    sqlite3_stmt *st;
    const char *sql = "SELECT invoices.id, number, date, clients.name, total_ttc, status FROM invoices JOIN clients ON clients.id=invoices.client_id ORDER BY invoices.id DESC";
    if (sqlite3_prepare_v2(app->db, sql, -1, &st, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        GtkTreeIter it;
        gtk_list_store_append(app->invoices_store, &it);
        gtk_list_store_set(app->invoices_store, &it,
                          0, sqlite3_column_int(st,0),
                          1, sqlite3_column_text(st,1),
                          2, sqlite3_column_text(st,2),
                          3, sqlite3_column_text(st,3),
                          4, sqlite3_column_double(st,4),
                          5, sqlite3_column_text(st,5),
                          -1);
    }
    sqlite3_finalize(st);
    return SQLITE_OK;
}

int db_update_invoice_status(App *app, int invoice_id, const char *status) {
    if (!valid_invoice_status(status)) return SQLITE_MISUSE;
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(app->db, "UPDATE invoices SET status=? WHERE id=?", -1, &st, NULL) != SQLITE_OK) {
        return SQLITE_ERROR;
    }
    sqlite3_bind_text(st, 1, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, invoice_id);
    int rc = sqlite3_step(st) == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
    sqlite3_finalize(st);
    return rc;
}
