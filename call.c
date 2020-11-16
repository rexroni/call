#include <signal.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>

#include <pjlib.h>
#include <pjlib-util.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>
#include <pjsip.h>
#include <pjsip_simple.h>
#include <pjsip_ua.h>
#include <pjsua-lib/pjsua.h>

#include <unistd.h>

#include "config.h"

typedef struct {
    char phone_number[128];
    pjsua_acc_id aid;
    pjsua_call_id cid;
    bool rx;
} pjsip_globals_t;


// auto-answer
// TODO: not sure how to access pjsip_globals from this callback
static bool in_call = false;
static pjsua_call_id rx_call_id;
static void on_incoming_call(pjsua_acc_id acc_id, pjsua_call_id call_id,
                             pjsip_rx_data *rdata){
    // respond that we are ringing, makes other side hear ringing too
    pj_status_t pret = pjsua_call_answer(call_id, 180, NULL, NULL);
    if(pret != PJ_SUCCESS){
        fprintf(stderr, "failed to answer call");
        exit(1);
    }

    // ring
    FILE *f = popen(RING_COMMAND, "r");
    pid_t w = wait(NULL);
    pclose(f);

    // answer
    pret = pjsua_call_answer(call_id, 200, NULL, NULL);
    if(pret != PJ_SUCCESS){
        fprintf(stderr, "failed to answer call");
        exit(1);
    }
    in_call = true;
    rx_call_id = call_id;
}


// connect to media when it opens
static void on_call_media_state(pjsua_call_id cid){
    pjsua_call_info ci;

    pjsua_call_get_info(cid, &ci);

    if(ci.media_status == PJSUA_CALL_MEDIA_ACTIVE) {
        // When media is active, connect call to sound device.
        pjsua_conf_connect(ci.conf_slot, 0);
        pjsua_conf_connect(0, ci.conf_slot);
    }
}


static int should_cont = true;
static int soft_kill = true;
// the second time through this function it will just hard-exit the program
static void sigint_handler(int signum){
    if(soft_kill){
        printf("catching signal, exiting\n");
        fclose(stdin);
        should_cont = false;
        soft_kill = false;
    }else{
        exit(1);
    }
}


// exit automatically if call disconnects
static void on_call_state(pjsua_call_id cid, pjsip_event *e){
    (void)e;
    // get call state
    pjsua_call_info ci;
    pjsua_call_get_info(cid, &ci);
    // if disconnected, end the program
    if(ci.state == PJSIP_INV_STATE_DISCONNECTED){
        pjsua_conf_disconnect(ci.conf_slot, 0);
        pjsua_conf_disconnect(0, ci.conf_slot);
        should_cont = false;
    }
}


int dial_number(pjsip_globals_t *pg){
    // build sip url
    char sip_url[128];
    int sip_len = sprintf(sip_url, SIP_PRE "%s" SIP_POST, pg->phone_number);
    if(sip_len > sizeof(sip_url)-1 || sip_len < 0){
        fprintf(stderr, "error during sprintf\n");
        return 50;
    }
    pj_str_t pj_sip_url = {.ptr=sip_url, .slen=sip_len};
    pjsua_call_setting cs;
    pjsua_call_setting_default(&cs);

    // wait some time
    // usleep(10000000);

    // make call
    pjsua_call_make_call(pg->aid, &pj_sip_url, &cs, NULL, NULL, &pg->cid);
    return 0;
}


int reg_unreg(pjsip_globals_t *pg){
    int retval = 0;

    // first prepare the creds
    // account registered to 9708187541
    pjsip_cred_info creds = {
        .realm = pj_str(REALM),
        .scheme = pj_str("digest"),
        .username = pj_str(USERNAME),
        .data_type = PJSIP_CRED_DATA_PLAIN_PASSWD,
        .data = pj_str(PASSWORD),
    };

    // then prepare an account config
    pjsua_acc_config ac;
    pjsua_acc_config_default(&ac);
    ac.id = pj_str(ACCOUNT_ID);
    ac.reg_uri = pj_str(REGISTER_URI);
    ac.cred_info[0] = creds;
    ac.cred_count = 1;

    // create the account
    int make_default = 1;
    pj_status_t pret = pjsua_acc_add(&ac, make_default, &pg->aid);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 40;
    }

    // prepare the terminal
    struct termios old_tios;
    // store terminal settings
    int ret = tcgetattr(0, &old_tios);
    if(ret != 0){
        perror("tcgetattr");
        exit(1);
    }

    struct termios new_tios = old_tios;
    // turn off echo and make terminal non-blocking
    new_tios.c_lflag &= ~(ICANON | ECHO);
    new_tios.c_cc[VMIN] = 0;
    new_tios.c_cc[VTIME] = 0;
    ret = tcsetattr(0, TCSANOW, &new_tios);
    if(ret != 0){
        perror("tcsetattr");
        return 41;
    }


    // dial
    if(!pg->rx){
        retval = dial_number(pg);
        if(retval !=0) goto call_done;
    }

    // wait for call to end, or SIGINT to be received
    while(should_cont){
        // try to read an ascii 0-9 char from stdin
        char c = 0;
        ssize_t read_ret = read(0, &c, 1);
        if(read_ret == 0 ||
                (read_ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))){
            // nothing to read, wait .1 seconds and try again
            usleep(100000);
            continue;
        }
        if(read_ret == -1){
            perror("read from stdin");
            retval = 44;
            goto call_done;
        }

        // make sure we got a digit
        if(!(c >= '0' && c <= '9') && c != '#' && c != '*'){
            printf("invalid dtmf character: %c\r\n", c);
            continue;
        }

        // make sure we either dialed, or if we received and are in_call
        if(!pg->rx || in_call){
            // send the digit
            pj_str_t digit = {.ptr=&c, .slen=1};
            pjsua_call_id call_id = pg->rx ? rx_call_id : pg->cid;
            pret = pjsua_call_dial_dtmf(call_id, &digit);
        }
    }

    // hang up if we either dialed, or if we received and are in_call
    if(!pg->rx || in_call){
        // hangup nicely
        pjsua_call_hangup(pg->rx ? rx_call_id : pg->cid, 0, NULL, NULL);
    }

call_done:
    ret = tcsetattr(0, TCSANOW, &old_tios);
    if(ret != 0){
        perror("tcsetattr");
        return 45;
    }

    pret = pjsua_acc_del(pg->aid);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 46;
    }
    return retval;
}


int pjstart(pjsip_globals_t *pg){

    pj_status_t pret = pjsua_start();
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 30;
    }

    /* I know there's no echo from my headset side, but I hear a slight echo
       on the phone side anyway.  I thought that might be due to ghost echos
       created by echo cancellation, but when I disabled echo cancellation it
       only got worse. */
    // // disable echo cancellation
    // pret = pjsua_set_ec(0, 0);
    // if(pret != PJ_SUCCESS){
    //     //psjua_perror("sender", "title", pret);
    //     return 32;
    // }

    pjmedia_snd_dev_info info[100];
    unsigned count = 100;

    // enumerate sound devices
    pret = pjsua_enum_snd_devs(info, &count);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 33;
    }

    // find the pulse sound device
    int pulse = -1;
    printf("sound device count = %u\n", count);
    for(unsigned i = 0; i < count; i++){
        pjmedia_snd_dev_info inf = info[i];
        printf(
            "sound device name: \"% .40s\" inputs: %u outputs %u\n",
            inf.name, inf.input_count, inf.output_count
        );

        if(strcmp(inf.name, "pulse") == 0){
            pulse = i;
        }
    }
    if(pulse < 0){
        printf("did not find pulse sound device!\n");
        return 34;
    }

    // validate the pulse sound device
    pjmedia_snd_dev_info pulse_dev = info[pulse];
    if(info[pulse].input_count == 0){
        printf("pulse has no inputs!\n");
        return 35;
    }
    if(info[pulse].output_count == 0){
        printf("pulse has no ouputs!\n");
        return 36;
    }

    // use the pulse sound device
    pret = pjsua_set_snd_dev(pulse, pulse);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 35;
    }

    return reg_unreg(pg);
}


int sip_transport(pjsip_globals_t *pg){
    int retval = 0;

    // start with default settings
    pjsua_transport_config tc;
    pjsua_transport_config_default(&tc);
    //tc.port = 5060;
    // tc.tls_setting // Holy shit, that's complicated.  We'll try again later.

    // create the transport
    pjsip_transport_type_e type = PJSIP_TRANSPORT_UDP;
    pjsua_transport_id tid;
    pj_status_t pret = pjsua_transport_create(type, &tc, &tid);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 20;
    }

    retval = pjstart(pg);

    pret = pjsua_transport_close(tid, 0);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 21;
    }
    return retval;
}


int setup_teardown(pjsip_globals_t *pg){
    int retval = 0;

    pj_status_t pret = pjsua_create();
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 11;
    }

    // all args are optional here
    pjsua_config pc;
    pjsua_config_default(&pc);
    // callback so we know when call is disconnected
    pc.cb.on_call_state = &on_call_state;
    // answer incoming calls?
    if(pg->rx){
        pc.cb.on_incoming_call = &on_incoming_call;
    }
    // callback to connect to opened media stream
    pc.cb.on_call_media_state = &on_call_media_state;
    pret = pjsua_init(&pc, NULL, NULL);
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        retval = 12;
        goto done;
    }

    retval = sip_transport(pg);

done:
    pret = pjsua_destroy();
    if(pret != PJ_SUCCESS){
        //psjua_perror("sender", "title", pret);
        return 13;
    }
    return retval;
}

int main(int argc, char** argv){
    pjsip_globals_t pg;
    if(argc < 2){
        pg.rx = true;
    }else{
        pg.rx = false;
        /* Grab phone number for later use.  It can have spaces or non-digits,
           but they will just get dropped.  It is utf8-stoopid. */
        size_t idx = 0;
        size_t argidx = 1;
        size_t argpos = 0;
        while(idx < sizeof(pg.phone_number) - 1 && argidx < argc){
            char c = argv[argidx][argpos];
            if(c == '\0'){
                // next arg
                argpos = 0;
                argidx++;
                continue;
            }
            if(c >= '0' && c <= '9'){
                // keep character
                pg.phone_number[idx++] = c;
            }
            // next character
            argpos++;
        }
        // null terminate
        pg.phone_number[idx] = '\0';
    }

    // set sigint
    signal(SIGINT, sigint_handler);

    return setup_teardown(&pg);
}
