#include "server.h"
#include "dialog.h"
#include <iostream>

Server::Server(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);
	filterWin = new FilterDialog(this);

    Dialog * x = new Dialog();
    x->exec();
    int tport = x->port;

	tcpServer = new QTcpServer(this);
    if (!tcpServer->listen(QHostAddress::Any, tport))
	{
		QMessageBox::critical(this, tr("Chat Server"),
            tr("无法启动服务器: %1.").arg(tcpServer->errorString()));
		close();
		return;
	}

	int port = tcpServer->serverPort();
    if(port != tport)
        QMessageBox::information(this, tr("Server"), tr("端口 %1 不合法,已重新分配 %2 .").arg(tport).arg(port));
    ui.statusText->setText(tr("监听端口: %1").arg(port));

	QTimer *timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), this, SIGNAL(SEND_UserList()));
	timer->start(6000);

	connect(tcpServer, SIGNAL(newConnection()), this, SLOT(newConnection()));
	connect(this, SIGNAL(newConnection()), this, SLOT(getMessage()));
	connect(this, SIGNAL(SEND_UserList()), this, SLOT(sendUserList()));
	connect(ui.filterButton, SIGNAL(clicked()), this, SLOT(showFilteredResults()));
	connect(ui.sendButton, SIGNAL(clicked()), this, SLOT(serverSendAll()));
}

Server::~Server()
{

}

void Server::newConnection()
{
	QTcpSocket *newSocket = tcpServer->nextPendingConnection();
	connect(newSocket, SIGNAL(disconnected()), this, SLOT(onDisconnect()));
	connect(newSocket, SIGNAL(disconnected()), this, SLOT(sendUserList()));
	connect(newSocket, SIGNAL(readyRead()), this, SLOT(getMessage()));
	clientConnections.append(newSocket);

	QByteArray block;
	QDataStream out(&block, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_0);

    QString message = tr("%1 服务器：已连接！").arg(timestamp());
	out << message;
	newSocket->write(block);
}

void Server::onDisconnect()
{
	QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

	if (socket != 0)
	{
		std::vector<int> currentSockets;

		// Look for sockets that aren't connected
		int socketID = 0;
		for (auto i : clientConnections)
		{
			currentSockets.push_back(i->socketDescriptor());
		}
		for (auto i : userList)
		{
			bool found = true;
			for (auto ii : currentSockets)
			{
				if (i.first == ii)
					found = false;
			}
			if (found == true)
			{
				socketID = i.first;
			}
		}

		QString username = getUsername(socketID);

		auto iter = userList.find(socketID);
		if (iter != userList.end())
			userList.erase(iter);

		ui.userList->clear();
		for (auto i : userList)
		{
			new QListWidgetItem(i.second, ui.userList);
		}

		clientConnections.removeAll(socket);
		socket->deleteLater();
        updateStatus("连接中断：(" + username + ":" + QString::number(socketID) + ")");
	}
}

void Server::sendMessage(QString message, QTcpSocket& socket)
{
	QByteArray msg;
	QDataStream out(&msg, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_0);

    message = timestamp() + " " + message;
	out << message;

	socket.write(msg);
}

void Server::sendToAll(QString message)
{
	QByteArray msg;
	QDataStream out(&msg, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_0);

    message = timestamp() + " " + message;
	out << message;

	for (auto i : clientConnections)
	{
		i->write(msg);
	}
}

void Server::sendToID(QString message, int ID)
{
	QByteArray msg;
	QDataStream out(&msg, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_0);

    message = timestamp() + " " + message;
	out << message;

	for (auto i : clientConnections)
	{
		if (i->socketDescriptor() == ID)
			i->write(msg);
	}
}

void Server::getMessage()
{
	QTcpSocket *client = qobject_cast<QTcpSocket*>(sender());
	QDataStream in(client);
	in.setVersion(QDataStream::Qt_4_0);

	QString message;
	in >> message;
    if(message.trimmed()=="")
        return;

	QStringList messageTokens;
	messageTokens = message.split(" ", QString::SkipEmptyParts);

	enum class COMMAND { NONE, USERNAME, USERCMD};
	COMMAND cmd = COMMAND::NONE;

	if (message == "_USR_")
		cmd = COMMAND::USERNAME;

	if (messageTokens.at(0) == "_UCD_")
	{
		cmd = COMMAND::USERCMD;
	}

	switch (cmd)
	{
	case COMMAND::USERNAME:
	{

		in >> message;

		QString username = message;
		QString tempname = username;
		int ID = client->socketDescriptor();

		//Check if username is taken
		int numInc = 0;
		bool isTaken = true;

		while (isTaken)
		{
			isTaken = false;
			for (auto i : userList)
			{
				if (i.second == tempname)
				{
					isTaken = true;
					++numInc;
				}
			}

			if (isTaken)
			{
				tempname = username + "(" + QString::number(numInc) + ")";
			}
		}

		username = tempname;

		userList.insert(std::make_pair(ID, username));
		new QListWidgetItem(username, ui.userList);
		ui.userList->scrollToBottom();

		QString address = getSocket(ID)->peerAddress().toString();
		QStringList addr = address.split(":", QString::SkipEmptyParts);
		address = addr.takeLast();

        QString newConnectionMsg = "新连接 (" + username + ":" + QString::number(ID) + "->" + address + ")";
		updateStatus(newConnectionMsg);
		break;
	}
	case COMMAND::USERCMD:
	{
		messageTokens.removeFirst();
		message.clear();
		for (auto i : messageTokens)
		{
			message += i;
			message += " ";
		}
		doCommand(message, client->socketDescriptor());
		break;
	}
	default:
		std::map<int, QString>::iterator it;
		it = userList.find(client->socketDescriptor());
        updateStatus("消息: (" + it->second + ") " + message);

		it = userList.find(client->socketDescriptor());
		QString usr = it->second;
		message = usr + ": " + message;
		sendToAll(message);
	}
}

void Server::updateStatus(QString message)
{
	message = timestamp() + " " + message;
	new QListWidgetItem(message, ui.statusList);
	ui.statusList->scrollToBottom();
}

void Server::sendUserList()
{
	QByteArray block;
	QDataStream out(&block, QIODevice::WriteOnly);
	out.setVersion(QDataStream::Qt_4_0);
	QString userlist;

	userlist = "_LST_";
	for (auto i : userList)
	{
		userlist += " ";
		userlist += i.second;
	}

	out << userlist;

	for (auto i : clientConnections)
	{
		i->write(block);
	}
}

void Server::doCommand(QString command, int ID)
{
	QString message = "Server: ";
	QStringList commandTokens = command.split(" ", QString::SkipEmptyParts);
	command = commandTokens.takeFirst();
	if (command == "/hello")
	{
		message += "Hi.";
	}
	else if (command == "/msg" || command == "/whisper" || command == "/pm")
	{
		if (!commandTokens.isEmpty())
		{
			QString recipient = commandTokens.takeFirst();
			int rID = 0;
			for (auto i : userList)
			{
				if (i.second == recipient)
					rID = i.first;
			}

			if (rID == 0)
                message += tr("用户名 \"%1\" 未找到").arg(recipient);
			else if (rID == ID)
                message += "你不能私信自己";
			else
			{
				QString text;
				text.clear();
				for (auto i : commandTokens)
				{
					text += i;
					text += " ";
				}

				auto user = userList.find(ID);

                if(ui.checkBox->isChecked())
                {
                    message = "* 发送到: " + recipient + ": " + text;
                    QString rMessage = "* 从: " + user->second + ": " + text;
                    sendToID(rMessage, rID);

                    QString status = "私信: (" + user->second + " -> " + recipient + ") " + text;
                    new QListWidgetItem(status, ui.statusList);
                }
                else
                    sendToID("对不起，服务器禁止私信！",ID);
			}
		}
		else
		{
            message = "*** 错误: 不正确 " + command + " 语法\n"
                + "*** 使用: " + command + "[用户名] [消息]";
		}
	}
	else if (command == "/help")
	{
        message = "** 帮助 **\n";
		message += '**\n';

		// Private messaging
        message += "** 使用 /msg, /pm 或 /whisper 来私信另一个用户\n";
        message += "** 语法: /msg [用户名] [消息]";
        message += "** 例子: /msg Lucky07 Hey, when did you get on?\n";
		message += '**\n';

		// Server reply
        message += "** 使用 /hello 来检查服务器的响应";
	}
	else
	{
        message += "无效的命令";
	}


	// return to sender
	sendToID(message, ID);
}

QTcpSocket* Server::getSocket(int ID)
{
	QTcpSocket* socket;

	for (auto i : clientConnections)
	{
		if (i->socketDescriptor() == ID)
			socket = i;
	}

	return socket;
}

QString Server::getUsername(int ID)
{
	auto itr = userList.find(ID);
	return itr->second;
}

void Server::showFilteredResults()
{
	filterWin->filterList->clear();
	QString filterText = ui.inputLine->text();

	for (int i = 0; i < ui.statusList->count(); ++i)
	{
		QString searchIn = ui.statusList->item(i)->text();
		if (searchIn.contains(filterText, Qt::CaseInsensitive))
		{
			new QListWidgetItem(searchIn, filterWin->filterList);
		}
	}

	filterWin->exec();
}

void Server::serverSendAll()
{
    QString tmp = ui.inputLine->text();
    if (tmp.trimmed() == "")
        return;
    QString inputText = "服务器: " + tmp;
	sendToAll(inputText);
	updateStatus(inputText);
	ui.inputLine->clear();
}

QString Server::timestamp()
{
	QStringList time = QDateTime::currentDateTime().toString().split(" ", QString::SkipEmptyParts);
	time.removeLast();
	time.removeFirst();
	time.removeFirst();
	time.removeFirst();

	return "[" + time.takeFirst() + "]";
}

void Server::on_userList_itemDoubleClicked(QListWidgetItem *item)
{
    if(QMessageBox::question(this,"询问",tr("是否断开用户%1?").arg(item->text()))==QMessageBox::Yes)
        for(auto i:userList)
            if(i.second==item->text())
            {
                int id=i.first;
                for(auto j:clientConnections)
                    if(j->socketDescriptor()==id)
                    {
                        j->abort();
                        userList.erase(i.first);
                        sendUserList();
                        return;
                    }
            }
}
