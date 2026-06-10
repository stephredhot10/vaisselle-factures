#ifndef VAISSELLE_APP_H
#define VAISSELLE_APP_H

#include <gtk/gtk.h>
#include <sqlite3.h>

typedef struct {
    int id;
    char name[256];
    char address[512];
    char email[256];
    char phone[128];
} Client;

typedef struct {
    char description[256];
    double quantity;
    double unit_price;
    double vat_rate;
} InvoiceLine;

typedef struct {
    sqlite3 *db;
    GtkWidget *window;
    GtkWidget *company_name, *company_siret, *company_address, *company_email, *company_phone, *company_legal;
    GtkWidget *client_name, *client_address, *client_email, *client_phone, *client_combo;
    GtkWidget *line_desc, *line_qty, *line_price, *line_vat;
    GtkWidget *lines_view, *invoices_view, *status_label;
    GtkListStore *clients_store, *lines_store, *invoices_store;
} App;

int db_open(App *app);
void db_close(App *app);
int db_init(sqlite3 *db);
int db_save_company(App *app);
int db_load_company(App *app);
int db_add_client(App *app);
int db_load_clients(App *app);
int db_create_invoice(App *app, int client_id);
int db_load_invoices(App *app);
int db_update_invoice_status(App *app, int invoice_id, const char *status);

char *invoice_export_html(App *app, int invoice_id);
const char *invoice_export_error_message(void);
void show_error(GtkWindow *parent, const char *message);
void set_status(App *app, const char *message);
char *app_data_path(const char *name);

#endif
