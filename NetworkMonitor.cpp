#include "NetworkMonitor.h"
#include "ui_NetworkMonitor.h"
#include "Logger.h"
#include "DBUtil.h"
#include "Consts.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QNetworkInterface>
#include <QDebug>
NetworkMonitor::NetworkMonitor(QWidget *parent) :
	QWidget(parent),
	ui(new Ui::NetworkMonitor)
{
	ui->setupUi(this);

	//初始化 网络管理器
	netManager=new QNetworkAccessManager();
	reply=NULL;
	/*初始化 网络请求*/
	//初始化 https策略
	QSslConfiguration sslConfig;
	sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
	sslConfig.setProtocol(QSsl::TlsV1_0);
	//初始化 内网检测地址(vUPC)
	vUPC.setUrl(QUrl("http://v.upc.edu.cn"));
	//初始化 网号登录状态地址(getUserInfo)
	netIDInfo.setUrl(QUrl("http://121.251.251.207/eportal/InterFace.do?method=getOnlineUserInfo"));
	netIDInfo.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 外网检测地址(ALiECS)
	aliECS.setUrl(QUrl("http://ip.since2014.cn"));
	aliECS.setSslConfiguration(sslConfig);
	aliECS.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 DNS检测地址(DNSPod)
	DNSReader.setUrl(QUrl("https://dnsapi.cn/Record.Info"));
	DNSReader.setSslConfiguration(sslConfig);
	DNSReader.setHeader(QNetworkRequest::UserAgentHeader,"RaspiFace/1.0(i@jerryzone.cn)");
	DNSReader.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 DNS设定地址(DNSPod)
	DNSPoster.setUrl(QUrl("https://dnsapi.cn/Record.Modify"));
	DNSPoster.setSslConfiguration(sslConfig);
	DNSPoster.setHeader(QNetworkRequest::UserAgentHeader,"RaspiFace/1.0(i@jerryzone.cn)");
	DNSPoster.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 登录页面检查地址
	pgCheck.setUrl(QUrl("http://121.251.251.207/eportal/InterFace.do?method=pageInfo"));
	pgCheck.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 网号登录地址
	netLogin.setUrl(QUrl("http://121.251.251.207/eportal/InterFace.do?method=login"));
	netLogin.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
	//初始化 验证码获取地址
	vCodeFetch.setUrl(QUrl("http://121.251.251.207/eportal/validcode"));
	//初始化 网号登录必需参数
	netLoginQS="";
	validCode="";
	//直接显示内网IP
	showEthIP();
	//初始化 计时器
	ui->freshTime->setText(QDateTime::currentDateTime().addSecs(5).toString("HH:mm:ss"));
	setTiming(5);
	QTimer::singleShot(0,this,SLOT(beat()));
	//初始化 按钮
	connect(ui->freshNow,SIGNAL(clicked()),this,SLOT(refreshImmediately()));
}

NetworkMonitor::~NetworkMonitor(){
	delete ui;
}
/*开始检测*/
void NetworkMonitor::startCheck(){
	ui->freshNow->setDisabled(true);
	showEthIP();
	ethCKRequest();
}
/*发起 IP获取&内网检测*/
void NetworkMonitor::ethCKRequest(){
	Logger::log("正在检测校园网连接...");
	if(reply) delete reply;
	reply=netManager->get(vUPC);
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(ethCKRequestRespond()));
}
/* IP获取&内网检测 响应 */
void NetworkMonitor::ethCKRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(ethCKRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("校园网检测超时!");
		return;
	}
	if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)
		return checkTerminated("校园网连接中断!");
	QString body=QString(reply->readAll());
	QRegExp rx("(\\d{1,3}\\.){3}\\d{1,3}");
	int pos = 0;
	QStringList IPS;
	while ((pos = rx.indexIn(body, pos)) != -1){
		pos += rx.matchedLength();
		QString result = rx.cap(0);
		IPS << result;
	}
	if(IPS.count()<=0)
		return checkTerminated("查询公网IP失败!");
	ui->intIP->setText(IPS[IPS.count()-1]);
	Logger::info("校园网连接正常!");
	idInfRequest();
}
/*发起 检查网号登录状态*/
void NetworkMonitor::idInfRequest(){
	Logger::log("正在拉取网号状态...");
	if(reply) delete reply;
	reply=netManager->post(netIDInfo,QByteArray("userIndex="));
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(idInfRequestRespond()));
}
/*网号状态拉取 请求响应*/
void NetworkMonitor::idInfRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(idInfRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		ui->account->setText("[拉取超时]");
		ui->name->setText("[拉取超时]");
		ui->remainTime->setText("[拉取超时]");
		checkTerminated("拉取网号状态数据超时!");
		return;
	}
	if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){
		ui->account->setText("[通信失败]");
		ui->name->setText("[通信失败]");
		ui->remainTime->setText("[通信失败]");
		checkTerminated("认证服务器返回异常!");
		return;
	}
	QJsonDocument qjd=QJsonDocument::fromJson(reply->readAll());
	QJsonObject stat=qjd.object();
	QJsonValue userId=stat.value("userId");
	QJsonValue userName=stat.value("userName");
	QJsonValue ballInfoRaw=stat.value("ballInfo");
	if(userId.isNull()){
		ui->account->setText("[未登录网号]");
		Logger::warning("未登网号,尝试登录网号!");
		loginPreRequest();
		return;
	}
	currentId=userId.toString("解析失败");
	currentName=userName.toString("解析失败");
	Logger::info(QString("网号:%1(%2)").arg(currentId,currentName));
	QJsonDocument ballInfo=QJsonDocument::fromJson(ballInfoRaw.toString("").toUtf8());
	QJsonArray balls=ballInfo.array();
	QString remainDate="";
	if(balls.size()>2){
		QJsonObject dateBall=balls.at(1).toObject();
		netIdDeadline=dateBall.value("value").toString().toULong();
		int remainTime=netIdDeadline-QDateTime::currentDateTime().toTime_t();
		remainDate=formatTime(remainTime);
		if(remainTime>86400){
			Logger::info("网号剩余时间充足"+remainDate);
			ui->remainTime->setStyleSheet("color:#000000");
		}else if(remainTime>0){
			Logger::warning("网号即将过期"+remainDate);
			ui->remainTime->setStyleSheet("color:#808000");
		}else{
			remainDate="[已到期]";
			Logger::error("网号已经到期,即将断网!");
			ui->remainTime->setStyleSheet("color:#FF0000");
		}
	}
	ui->account->setText(currentId);
	ui->name->setText(currentName);
	ui->remainTime->setText(remainDate);
	aliECSRequest();
}
/* 发起 阿里云转储请求*/
void NetworkMonitor::aliECSRequest(){
	Logger::log("正在更新阿里云状态...");
	if(reply) delete reply;
	QString raw("TG=Raspi&IP=%1&timestamp=%2&Life=1200");
	raw=raw.arg(ui->intIP->text(),QString::number(QDateTime::currentDateTime().toTime_t()));
	reply=netManager->post(aliECS,raw.toUtf8());
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(aliECSRequestRespond()));
}
/* 检查网络 请求响应*/
void NetworkMonitor::aliECSRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(aliECSRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("更新阿里云超时!");
		return;
	}
	if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=666){
		checkTerminated("公网异常稍后重试!");
		return;
	}
	Logger::info("阿里云已经更新!");
	dnsCKRequest();
}
/*发起 DNS检查*/
void NetworkMonitor::dnsCKRequest(){
	Logger::log("正在检查DNS数据...");
	QByteArray data("login_token=19255,ed8e437c6e7a30b57a60aa1e787b9940&format=json&domain=jerryhome.tk&record_id=195272416");
	if(reply) delete reply;
	reply=netManager->post(DNSReader,data);
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(dnsCKRequestRespond()));
}
/*DNS检查 请求响应*/
void NetworkMonitor::dnsCKRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(dnsCKRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("请求DNSPod超时!");
		return;
	}
	if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)!=200)
		return checkTerminated("检查DNS时发生HTTP错误!");
	QJsonDocument QJDom=QJsonDocument::fromJson(reply->readAll());
	if(QJDom.object().take("status").toObject().take("code").toString().toInt()!=1)
		checkTerminated("检查DNS时发生鉴权错误!");
	else if(QJDom.object().take("record").toObject().take("value").toString()!=ui->intIP->text()){
		Logger::warning("DNS信息过期!");
		dnsSTRequest();
	}else{
		Logger::info("DNS信息无需更新!");
		checkFinished();
	}
}
/*发起 DNS设定*/
void NetworkMonitor::dnsSTRequest(){
	QString raw="login_token=19255,ed8e437c6e7a30b57a60aa1e787b9940&format=json&domain=jerryhome.tk&record_id=195272416&sub_domain=www&record_type=A&record_line=%e9%bb%98%e8%ae%a4&value=%1";
	raw=raw.arg(ui->intIP->text());
	Logger::log("正在更新DNS数据...");
	if(reply) delete reply;
	reply=netManager->post(DNSPoster,raw.toLatin1());
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(dnsSTRequestRespond()));
}
/*DNS设定 请求响应*/
void NetworkMonitor::dnsSTRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(dnsSTRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("请求DNSPod超时!");
		return;
	}
	if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)!=200)
		return checkTerminated("设定DNS时发生HTTP错误!");
	QJsonDocument QJDom=QJsonDocument::fromJson(reply->readAll());
	if(QJDom.object().take("status").toObject().take("code").toString().toInt()!=1)
		return checkTerminated("设定DNS失败!");
	Logger::info("DNS更新成功!");
	checkFinished();
}
/*发起 登录网号预备*/
void NetworkMonitor::loginPreRequest(){
	Logger::log("正在检测认证服务器状态...");
	if(reply) delete reply;
	reply=netManager->get(aliECS);
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(loginPreRequestRespond()));
}
/*登录网号预备 响应*/
void NetworkMonitor::loginPreRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(loginPreRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("认证服务器请求超时!");
		return;
	}else if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)==200){
		Logger::info("疑似网络正常,重拉网号状态!");
		idInfRequest();
		return;
	}else if(!reply->hasRawHeader("Location")){
		checkTerminated("认证服务器返回异常!");
		return;
	}
	Logger::info("认证服务器工作正常");
	QString newLocation=reply->header(QNetworkRequest::LocationHeader).toString();
	netLoginQS=newLocation.split("?").at(1);
	loginPgChkRequest();
	//loginRequest();
}
/*发起 页面检查请求*/
void NetworkMonitor::loginPgChkRequest(){
	Logger::log("检查验证码需求...");
	QString raw=QString("queryString=%1").arg(netLoginQS);
	if(reply) delete reply;
	reply=netManager->post(pgCheck,raw.toLatin1());
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(loginPgChkRequestRespond()));
}
/*页面检查 请求响应*/
void NetworkMonitor::loginPgChkRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(loginPgChkRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("登录网号超时!");
		return;
	}else if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){
		checkTerminated("登录网号时发生网络错误!");
		return;
	}
	QJsonObject data=QJsonDocument::fromJson(reply->readAll()).object();
	QString vCodeUrl=data.take("validCodeUrl").toString();
	if(vCodeUrl.length()<1){
		Logger::warning("认证系统请求验证码!");
		vCodeRequest();
		return;
	}
	loginRequest();
}

/*发起 登录网号*/
void NetworkMonitor::loginRequest(){
	QString raw("operatorPwd=&operatorUserId=&password=%2&queryString=%3&service=&userId=%1&validcode=%4");
	raw=raw.arg("1409030301","123456Abc",QString(netLoginQS.toLocal8Bit().toPercentEncoding()),validCode);
	if(reply) delete reply;
	reply=netManager->post(netLogin,raw.toLatin1());
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(loginRequestRespond()));
}
/*登网号 请求响应*/
void NetworkMonitor::loginRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(loginRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("登录网号超时!");
		return;
	}else if(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){
		checkTerminated("登录网号时发生网络错误!");
		return;
	}
	QJsonDocument qjd=QJsonDocument::fromJson(reply->readAll());
	QJsonObject stat=qjd.object();
	QString result=stat.value("result").toString();
	if(result=="success"){
		Logger::info("登录网号成功!");
		validCode="";
		idInfRequest();
	}else{
		QString Reason=stat.value("message").toString();
		Logger::error(QString("登录失败:%1").arg(Reason));
		if(Reason=="验证码错误.")
			vCodeRequest();
		else
			checkTerminated();
	}
}
/*更新验证码*/
void NetworkMonitor::vCodeRequest(){
	if(reply) delete reply;
	Logger::log("正在获取验证码...");
	reply=netManager->get(vCodeFetch);
	QTimer::singleShot(NET_WAITING_TIME,reply,SLOT(abort()));
	connect(reply,SIGNAL(finished()),this,SLOT(vCodeRequestRespond()));
}
/*更新验证码 响应槽*/
void NetworkMonitor:: vCodeRequestRespond(){
	disconnect(reply,SIGNAL(finished()),this,SLOT(loginRequestRespond()));
	if(reply->error()==QNetworkReply::OperationCanceledError){
		reply->disconnect();
		checkTerminated("获取验证码超时!");
		return;
	}
	QByteArray data=reply->readAll();
	QString picMD5=QString(QCryptographicHash::hash(data,QCryptographicHash::Md5).toHex());
	QSqlQuery* sqlHandle=DBUtil::getSqlHandle();
	sqlHandle->prepare("Select code from vcode where md5=?");
	sqlHandle->bindValue(0,picMD5);
	sqlHandle->exec();
	if(sqlHandle->next()){
		validCode=sqlHandle->value("code").toString();
		Logger::info("验证码:"+validCode);
		loginRequest();
	}else{
		checkTerminated("验证码识别失败!");
	}
}

/*检查错误中断*/
void NetworkMonitor::checkTerminated(QString reason){
	if(reason.count()>0) Logger::error(reason);
	setTiming(20);
	QTimer::singleShot(0,this,SLOT(beat()));
	ui->freshNow->setEnabled(true);
}
/*检查结束*/
void NetworkMonitor::checkFinished(){
	Logger::info("网络连接正常!");
	ui->freshTime->setText(QDateTime::currentDateTime().addSecs(1200).toString("HH:mm:ss"));
	setTiming(1200);
	QTimer::singleShot(0,this,SLOT(beat()));
	ui->freshNow->setEnabled(true);
}

void NetworkMonitor::beat(){
	timing--;
	ui->countBar->setValue(timing);
	//ui->networkTimeCount->setText(QString::number(timing));
	if(timing<=0)
		this->startCheck();
	else
		QTimer::singleShot(1000,this,SLOT(beat()));
}

/**
 * 普通信息获取
*/
/* 获取&显示内网IP*/
void NetworkMonitor::showEthIP(){
	QNetworkInterface eth0=QNetworkInterface::interfaceFromName("eth0");
	QList<QNetworkAddressEntry> entryList = eth0.addressEntries();
	QString EthIP;
	QRegExp ip("(\\d{1,3}\\.){3}\\d{1,3}");
	if(entryList.count()<=0)
		EthIP="---.---.---.---";
	else
		foreach(QNetworkAddressEntry temp,entryList){
			if(ip.exactMatch(temp.ip().toString().replace("\n",""))){
				EthIP=temp.ip().toString().replace("\n","");
				break;
			}
		}

	ui->ethIP->setText(EthIP);
}
void NetworkMonitor::setTiming(int length){
	timing=length;
	ui->countBar->setRange(0,length);
	ui->countBar->setValue(0);
}
void NetworkMonitor::refreshImmediately(){
	timing=0;
	ui->countBar->setValue(0);
	ui->freshTime->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
}
void NetworkMonitor::updateTime(){
	ui->freshTime->setText(QDateTime::currentDateTime().addSecs(timing).toString("HH:mm:ss"));
	if(netIdDeadline!=0)
		ui->remainTime->setText(formatTime(netIdDeadline-QDateTime::currentDateTime().toTime_t()));
}
QString NetworkMonitor::formatTime(int t){
	int timeVal[]={1,60,3600,86400};
	QString timeUnit[]={"秒","分","时","天"};
	int i=0;
	for(i=0;i<3&&timeVal[i]<t&&timeVal[i+1]<t;i++);
	return QString("[%1%2]").arg(QString::number(t/timeVal[i]),timeUnit[i]);
}
