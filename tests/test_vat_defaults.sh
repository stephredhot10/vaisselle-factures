#!/bin/sh
set -eu

fail() {
	printf 'FAIL: %s\n' "$1" >&2
	exit 1
}

if grep -Fq "TVA non applicable, art. 293 B du CGI" src/db.c; then
	fail "the company legal notice must not default to the VAT exemption notice"
fi

grep -Fq "VALUES(1,'','','','','','');" src/db.c ||
	fail "the company legal notice must default to an empty string"

grep -Fq "UPDATE company SET legal='' WHERE id=1" src/db.c ||
	fail "existing databases with the old VAT exemption default must be migrated"

grep -Fq "gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->line_vat),20);" src/main.c ||
	fail "new invoice lines must default to 20% VAT"

grep -Fq "app->line_price=gtk_spin_button_new_with_range(0,9999,0.01);" src/main.c ||
	fail "unit price spin button must allow cent-level prices such as 0.32"

grep -Fq "gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->line_price),2);" src/main.c ||
	fail "unit price spin button must display and preserve two decimal places"

printf 'VAT defaults and price precision: OK\n'
