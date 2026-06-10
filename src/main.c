#include "app.h"
#include <stdlib.h>
#include <string.h>

void show_error(GtkWindow *parent, const char *message) {
    GtkWidget *d = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}

void set_status(App *app, const char *message) { gtk_label_set_text(GTK_LABEL(app->status_label), message); }

static GtkWidget *entry(const char *placeholder) { GtkWidget *e=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(e), placeholder); return e; }
static GtkWidget *textview(void) { GtkWidget *v=gtk_text_view_new(); gtk_widget_set_size_request(v, 320, 80); return v; }
static void add_labeled(GtkWidget *grid, int row, const char *label, GtkWidget *w) { gtk_grid_attach(GTK_GRID(grid), gtk_label_new(label), 0,row,1,1); gtk_grid_attach(GTK_GRID(grid), w, 1,row,1,1); }

static GtkWidget *tree_col(GtkWidget *view, const char *title, int col, gboolean money) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    if (money) g_object_set(r, "xalign", 1.0, NULL);
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    return view;
}

static void on_save_company(GtkButton *b, gpointer data) { (void)b; App *app=data; if (db_save_company(app)==SQLITE_OK) set_status(app,"Entreprise enregistrée"); else show_error(GTK_WINDOW(app->window),"Impossible d’enregistrer l’entreprise"); }
static void on_add_client(GtkButton *b, gpointer data) {
    (void)b;
    App *app=data;
    int rc = db_add_client(app);
    if (rc == SQLITE_OK) {
        db_load_clients(app);
        gtk_entry_set_text(GTK_ENTRY(app->client_name),"");
        gtk_entry_set_text(GTK_ENTRY(app->client_email),"");
        gtk_entry_set_text(GTK_ENTRY(app->client_phone),"");
        GtkTextBuffer *addr = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->client_address));
        gtk_text_buffer_set_text(addr, "", -1);
        set_status(app,"Client ajouté");
    } else if (rc == SQLITE_MISUSE) {
        show_error(GTK_WINDOW(app->window),"Nom client invalide (1-255 caractères)");
    } else {
        show_error(GTK_WINDOW(app->window),"Erreur base de données lors de l'ajout du client");
    }
}

static void on_add_line(GtkButton *b, gpointer data) {
    (void)b; App *app=data; const char *desc=gtk_entry_get_text(GTK_ENTRY(app->line_desc));
    if (!desc || !*desc) { show_error(GTK_WINDOW(app->window),"Désignation obligatoire"); return; }
    double qty = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->line_qty));
    double price = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->line_price));
    if (price <= 0) {
        show_error(GTK_WINDOW(app->window), "Le prix unitaire doit être supérieur à 0");
        return;
    }
    if (qty <= 0) {
        show_error(GTK_WINDOW(app->window), "La quantité doit être supérieure à 0");
        return;
    }
    GtkTreeIter it; gtk_list_store_append(app->lines_store,&it);
    gtk_list_store_set(app->lines_store,&it,0,desc,1,gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->line_qty)),2,gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->line_price)),3,gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->line_vat)),-1);
    gtk_entry_set_text(GTK_ENTRY(app->line_desc),""); set_status(app,"Ligne ajoutée");
}

static int selected_client_id(App *app) { GtkTreeIter it; if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(app->client_combo), &it)) return 0; int id=0; gtk_tree_model_get(GTK_TREE_MODEL(app->clients_store), &it, 0, &id, -1); return id; }
static int selected_invoice_id(App *app) { GtkTreeSelection *sel=gtk_tree_view_get_selection(GTK_TREE_VIEW(app->invoices_view)); GtkTreeIter it; GtkTreeModel *m; if (!gtk_tree_selection_get_selected(sel,&m,&it)) return 0; int id=0; gtk_tree_model_get(m,&it,0,&id,-1); return id; }

static void on_create_invoice(GtkButton *b, gpointer data) { (void)b; App *app=data; int cid=selected_client_id(app); if (!cid) { show_error(GTK_WINDOW(app->window),"Sélectionner un client"); return; } int rc=db_create_invoice(app,cid); if (rc==SQLITE_OK) { gtk_list_store_clear(app->lines_store); db_load_invoices(app); set_status(app,"Facture créée"); } else if (rc==SQLITE_MISUSE) show_error(GTK_WINDOW(app->window),"Impossible de créer la facture : ajouter au moins une ligne valide"); else show_error(GTK_WINDOW(app->window),"Impossible de créer la facture : erreur base de données ou numérotation"); }
static void on_refresh_invoices(GtkButton *b, gpointer data) { (void)b; db_load_invoices(data); }
static void update_selected_status(App *app, const char *status) { int id=selected_invoice_id(app); if (!id) { show_error(GTK_WINDOW(app->window),"Sélectionner une facture"); return; } if (db_update_invoice_status(app,id,status)==SQLITE_OK) { db_load_invoices(app); set_status(app,"Statut mis à jour"); } else show_error(GTK_WINDOW(app->window),"Impossible de changer le statut"); }
static void on_status_sent(GtkButton *b, gpointer data) { (void)b; update_selected_status(data, "envoyée"); }
static void on_status_paid(GtkButton *b, gpointer data) { (void)b; update_selected_status(data, "payée"); }
static void on_status_draft(GtkButton *b, gpointer data) { (void)b; update_selected_status(data, "brouillon"); }
static void on_export(GtkButton *b, gpointer data) { (void)b; App *app=data; int id=selected_invoice_id(app); if (!id) { show_error(GTK_WINDOW(app->window),"Sélectionner une facture"); return; } char *p=invoice_export_html(app,id); if (p) { char *msg=g_strdup_printf("Export créé : %s", p); set_status(app,msg); g_free(msg); g_free(p); } else show_error(GTK_WINDOW(app->window), invoice_export_error_message()); }

static GtkWidget *company_tab(App *app) {
    GtkWidget *grid=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid),8); gtk_grid_set_column_spacing(GTK_GRID(grid),8); gtk_container_set_border_width(GTK_CONTAINER(grid),12);
    app->company_name=entry("Nom de l’entreprise"); app->company_siret=entry("SIRET"); app->company_address=textview(); app->company_email=entry("Email"); app->company_phone=entry("Téléphone"); app->company_legal=textview();
    add_labeled(grid,0,"Nom",app->company_name); add_labeled(grid,1,"SIRET",app->company_siret); add_labeled(grid,2,"Adresse",app->company_address); add_labeled(grid,3,"Email",app->company_email); add_labeled(grid,4,"Téléphone",app->company_phone); add_labeled(grid,5,"Mention légale",app->company_legal);
    GtkWidget *btn=gtk_button_new_with_label("Enregistrer entreprise"); gtk_grid_attach(GTK_GRID(grid),btn,1,6,1,1); g_signal_connect(btn,"clicked",G_CALLBACK(on_save_company),app); return grid;
}

static GtkWidget *clients_tab(App *app) {
    GtkWidget *grid=gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid),8); gtk_grid_set_column_spacing(GTK_GRID(grid),8); gtk_container_set_border_width(GTK_CONTAINER(grid),12);
    app->client_name=entry("Nom client"); app->client_address=textview(); app->client_email=entry("Email"); app->client_phone=entry("Téléphone");
    add_labeled(grid,0,"Nom",app->client_name); add_labeled(grid,1,"Adresse",app->client_address); add_labeled(grid,2,"Email",app->client_email); add_labeled(grid,3,"Téléphone",app->client_phone);
    GtkWidget *btn=gtk_button_new_with_label("Ajouter client"); gtk_grid_attach(GTK_GRID(grid),btn,1,4,1,1); g_signal_connect(btn,"clicked",G_CALLBACK(on_add_client),app); return grid;
}

static GtkWidget *invoice_tab(App *app) {
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8); gtk_container_set_border_width(GTK_CONTAINER(box),12);
    app->clients_store=gtk_list_store_new(2,G_TYPE_INT,G_TYPE_STRING); app->client_combo=gtk_combo_box_new_with_model(GTK_TREE_MODEL(app->clients_store)); GtkCellRenderer *r=gtk_cell_renderer_text_new(); gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(app->client_combo),r,TRUE); gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(app->client_combo),r,"text",1,NULL); gtk_box_pack_start(GTK_BOX(box),app->client_combo,FALSE,FALSE,0);
    GtkWidget *grid=gtk_grid_new(); gtk_grid_set_column_spacing(GTK_GRID(grid),8); app->line_desc=entry("Ex: Assiettes plates blanches"); app->line_qty=gtk_spin_button_new_with_range(0.01,9999,1); app->line_price=gtk_spin_button_new_with_range(0,9999,0.01); gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->line_price),2); app->line_vat=gtk_spin_button_new_with_range(0,100,1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->line_qty),1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->line_vat),20); gtk_grid_attach(GTK_GRID(grid),app->line_desc,0,0,1,1); gtk_grid_attach(GTK_GRID(grid),app->line_qty,1,0,1,1); gtk_grid_attach(GTK_GRID(grid),app->line_price,2,0,1,1); gtk_grid_attach(GTK_GRID(grid),app->line_vat,3,0,1,1); GtkWidget *add=gtk_button_new_with_label("Ajouter ligne"); gtk_grid_attach(GTK_GRID(grid),add,4,0,1,1); g_signal_connect(add,"clicked",G_CALLBACK(on_add_line),app); gtk_box_pack_start(GTK_BOX(box),grid,FALSE,FALSE,0);
    app->lines_store=gtk_list_store_new(4,G_TYPE_STRING,G_TYPE_DOUBLE,G_TYPE_DOUBLE,G_TYPE_DOUBLE); app->lines_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->lines_store)); tree_col(app->lines_view,"Désignation",0,FALSE); tree_col(app->lines_view,"Qté",1,FALSE); tree_col(app->lines_view,"PU HT",2,TRUE); tree_col(app->lines_view,"TVA %",3,FALSE); gtk_box_pack_start(GTK_BOX(box),app->lines_view,TRUE,TRUE,0);
    GtkWidget *create=gtk_button_new_with_label("Créer facture"); g_signal_connect(create,"clicked",G_CALLBACK(on_create_invoice),app); gtk_box_pack_start(GTK_BOX(box),create,FALSE,FALSE,0); return box;
}

static GtkWidget *invoices_tab(App *app) {
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_container_set_border_width(GTK_CONTAINER(box),12);
    app->invoices_store=gtk_list_store_new(6,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_DOUBLE,G_TYPE_STRING);
    app->invoices_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->invoices_store));
    tree_col(app->invoices_view,"ID",0,FALSE); tree_col(app->invoices_view,"Numéro",1,FALSE); tree_col(app->invoices_view,"Date",2,FALSE); tree_col(app->invoices_view,"Client",3,FALSE); tree_col(app->invoices_view,"TTC",4,TRUE); tree_col(app->invoices_view,"Statut",5,FALSE);
    gtk_box_pack_start(GTK_BOX(box),app->invoices_view,TRUE,TRUE,0);
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    GtkWidget *ref=gtk_button_new_with_label("Actualiser");
    GtkWidget *draft=gtk_button_new_with_label("Brouillon");
    GtkWidget *sent=gtk_button_new_with_label("Marquer envoyée");
    GtkWidget *paid=gtk_button_new_with_label("Marquer payée");
    GtkWidget *exp=gtk_button_new_with_label("Exporter HTML");
    g_signal_connect(ref,"clicked",G_CALLBACK(on_refresh_invoices),app);
    g_signal_connect(draft,"clicked",G_CALLBACK(on_status_draft),app);
    g_signal_connect(sent,"clicked",G_CALLBACK(on_status_sent),app);
    g_signal_connect(paid,"clicked",G_CALLBACK(on_status_paid),app);
    g_signal_connect(exp,"clicked",G_CALLBACK(on_export),app);
    gtk_box_pack_start(GTK_BOX(row),ref,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(row),draft,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(row),sent,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(row),paid,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(row),exp,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
    return box;
}

static void activate(GtkApplication *gtk_app, gpointer user_data) {
    App *app=user_data; app->window=gtk_application_window_new(gtk_app); gtk_window_set_title(GTK_WINDOW(app->window),"Vaisselle Factures"); gtk_window_set_default_size(GTK_WINDOW(app->window),900,620);
    GtkWidget *main=gtk_box_new(GTK_ORIENTATION_VERTICAL,0); GtkWidget *tabs=gtk_notebook_new(); gtk_notebook_append_page(GTK_NOTEBOOK(tabs), company_tab(app), gtk_label_new("Entreprise")); gtk_notebook_append_page(GTK_NOTEBOOK(tabs), clients_tab(app), gtk_label_new("Clients")); gtk_notebook_append_page(GTK_NOTEBOOK(tabs), invoice_tab(app), gtk_label_new("Facture")); gtk_notebook_append_page(GTK_NOTEBOOK(tabs), invoices_tab(app), gtk_label_new("Factures")); app->status_label=gtk_label_new("Prêt"); gtk_box_pack_start(GTK_BOX(main),tabs,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(main),app->status_label,FALSE,FALSE,6); gtk_container_add(GTK_CONTAINER(app->window),main);
    gtk_widget_show_all(app->window);
    if (db_open(app)!=SQLITE_OK) { show_error(GTK_WINDOW(app->window),"Impossible d’ouvrir la base SQLite"); set_status(app,"Erreur base de données"); return; }
    if (db_load_company(app)!=SQLITE_OK) { show_error(GTK_WINDOW(app->window),"Impossible de charger les données entreprise"); set_status(app,"Erreur chargement entreprise"); return; }
    if (db_load_clients(app)!=SQLITE_OK) { show_error(GTK_WINDOW(app->window),"Impossible de charger les clients"); set_status(app,"Erreur chargement clients"); return; }
    if (db_load_invoices(app)!=SQLITE_OK) { show_error(GTK_WINDOW(app->window),"Impossible de charger les factures"); set_status(app,"Erreur chargement factures"); return; }
}

int main(int argc, char **argv) { App app; memset(&app,0,sizeof(app)); GtkApplication *gapp=gtk_application_new("fr.cyril103.vaissellefactures", G_APPLICATION_DEFAULT_FLAGS); g_signal_connect(gapp,"activate",G_CALLBACK(activate),&app); int status=g_application_run(G_APPLICATION(gapp),argc,argv); db_close(&app); g_object_unref(gapp); return status; }
