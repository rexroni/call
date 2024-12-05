// HOW TO LOG IN
#define USERNAME "my-username"
#define PASSWORD "my-password"
#define REALM "my-server.voip.ms"
#define CREDS_SCHEME "digest"
#define CREDS_DATA_TYPE PJSIP_CRED_DATA_PLAIN_PASSWD

// WHERE TO LOG IN
#define ACCOUNT_ID "sip:" USERNAME "@" REALM
#define REGISTER_URI "sips:my-server1.voip.ms"

// HOW TO DIAL A PHONE NUMBER
#define SIP_URL_SPRINTF_ARGS(phone_number) \
    "sips:%s@my-server1.voip.ms", phone_number

// TLS SETTINGS
#define USE_TLS 1
#define TLS_BUNDLE_CHECK_PATHS { \
    "/etc/ssl/certs/ca-certificates.crt", /* for archlinux, debian */ \
    "/etc/pki/tls/certs/ca-bundle.crt", /* for fedora */ \
}
// one of PJMEDIA_SRTP_{MANDATORY,OPTIONAL,DISABLED}
#define USE_SRTP PJMEDIA_SRTP_MANDATORY
