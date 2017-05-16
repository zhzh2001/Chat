#include "dialog.h"
#include "ui_dialog.h"

#include <iostream>

Dialog::Dialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Dialog)
{
    ui->setupUi(this);
    port = 12234;
}

Dialog::~Dialog()
{
    delete ui;
}

void Dialog::on_pushButton_clicked()
{
    QString tmp = ui->lineEdit->text();
    port = tmp.toInt();
    this->close();
}
