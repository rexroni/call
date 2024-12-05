// HOW TO LOG IN
#define USERNAME "my-username"
#define PASSWORD "my-password"
#define REALM "my-server.voip.ms"
#define CREDS_SCHEME "digest"
#define CREDS_DATA_TYPE PJSIP_CRED_DATA_PLAIN_PASSWD

// WHERE TO LOG IN
#define ACCOUNT_ID "sip:" USERNAME "@" REALM
#define REGISTER_URI "sip:" REALM

// HOW TO DIAL A PHONE NUMBER
#define SIP_URL_SPRINTF_ARGS(phone_number) \
    "sip:%s@" REALM, phone_number

// TLS SETTINGS
#define USE_TLS 0
