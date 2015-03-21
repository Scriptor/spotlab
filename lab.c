#include "lab.h"

#include <pthread.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

sp_session *g_session;

static int notify_events;
static pthread_mutex_t notify_mut;
static pthread_cond_t notify_cond;

int can_prompt = 0;
static pthread_cond_t prompt_cond;
command_t *current_cmd;

void notify_main_thread(sp_session *session){
    pthread_mutex_lock(&notify_mut);
    notify_events = 1;
    pthread_cond_signal(&notify_cond);
    pthread_mutex_unlock(&notify_mut);
}

static void connection_error(sp_session *session, sp_error error)
{
	
}
static void logged_in(sp_session *session, sp_error error)
{

}

static void logged_out(sp_session *session)
{
	//exit(0);
}


/**
 * This callback is called for log messages.
 *
 * @sa sp_session_callbacks#log_message
 */
static void log_message(sp_session *session, const char *data)
{
    //printf("%s\n", data);
}

static sp_session_callbacks callbacks = {
    .logged_in = &logged_in,
    .logged_out = &logged_out,
    .connection_error = &connection_error,
    .notify_main_thread = &notify_main_thread,
    .log_message = &log_message,
};

static void artistbrowse_complete(sp_artistbrowse *result, void *name){
    int i, num_tracks, display_num;
    char *ar_name = (char *)name;
    sp_track *t;

    num_tracks = sp_artistbrowse_num_tracks(result);
    display_num = num_tracks < 5 ? num_tracks : 5;
    printf("%d tracks found for %s\n", num_tracks, ar_name);
    printf("Listing first %d\n", display_num);

    for(i=0; i<display_num; i++){
        t = sp_artistbrowse_track(result, i);
        printf("%d: %s\n", i, sp_track_name(t));
    }

    sp_artistbrowse_release(result);
    finish_cmd();
}

static void search_complete(sp_search *result, void *userdata){
    int num_artists;
    const char *name;
    sp_artist *ar;

    num_artists = sp_search_num_artists(result);
    printf("%d search artists found\n", num_artists);

    ar = sp_search_artist(result, 0);
    name = sp_artist_name(ar);

    printf("Initiating browse of artist: \"%s\"...\n", name);
    sp_artistbrowse_create(g_session, ar, SP_ARTISTBROWSE_FULL,
            &artistbrowse_complete, (void *)name);

    sp_search_release(result);
    sp_artist_release(ar);

    // Don't finish_cmd here because we need to wait
    // for the artistbrowse callback to finish
}

void parse_toks(char *cmd_str, tok_t **toks){
    char cur_tok[256];
    char c;
    int i=0, j=0;

    // Loop indefinitely with (|| 1)
    // break on null-terminator inside loop
    while((c = *cmd_str++) || 1){
        if(isspace(c) || c == '\0'){
            cur_tok[j] = '\0';
            strcpy(toks[i]->val, cur_tok);
            if(i == 3 || c == '\0'){
                break;
            }else{
                j=0;
                i++;
            }
        }else{
            cur_tok[j] = c;
            j++;
        }
    }
}

command_t *prompt(char *prompt_str){
    char *cmd_str;
    cmd_kind kind;
    command_t *cmd;
    tok_t *cmd_toks[4];
    int i;

    for(i=0; i<4; i++){
        cmd_toks[i] = (tok_t *)malloc(sizeof(tok_t));
    }

    cmd_str = readline(prompt_str);
    parse_toks(cmd_str, cmd_toks);
    free(cmd_str);

    if(strcmp(cmd_toks[0]->val, "search") == 0){
        kind = SEARCH;
    }else{
        kind = -1;
    }
    free(cmd_toks[0]);
    
    cmd = (command_t *)malloc(sizeof(command_t));
    for(i=0; i<3; i++){
        strcpy(cmd->args[i], cmd_toks[i+1]->val);
        free(cmd_toks[i+1]);
    }
    cmd->kind = kind;
    return cmd;
}

void *promptloop(void *_){
    pthread_mutex_lock(&notify_mut);

    while(1){
        while(!can_prompt)
            pthread_cond_wait(&prompt_cond, &notify_mut);

        current_cmd = prompt("spotlab> ");

        can_prompt = 0;
        pthread_cond_signal(&notify_cond);
        pthread_mutex_unlock(&notify_mut);
    }
}

void finish_cmd(){
    pthread_mutex_lock(&notify_mut);
    can_prompt = 1;
    pthread_cond_signal(&prompt_cond);
    pthread_mutex_unlock(&notify_mut);
}

void run_cmd(command_t *cmd){
    switch(cmd->kind){
    case SEARCH:
        sp_search_create(g_session, cmd->args[0], 0, 5, 0, 5, 0, 5, 0, 10,
                SP_SEARCH_STANDARD, &search_complete, NULL);
        break;
    case PLAY:
    default:
        finish_cmd();
        break;
    }
}

// Contains static configuration data
static sp_session_config config = {
    .api_version = SPOTIFY_API_VERSION,
    .cache_location = "tmp",
    .settings_location = "tmp",
    .application_key = g_appkey,
    .application_key_size = 0, // Set in main()
    .user_agent = "Scriptor's Lab",
    .callbacks = &callbacks,
    // Rest of config MUST be null or otherwise Spotify gets junk data
    // This causes proxy errors and more
    NULL,
};

int spotify_init(const char *username, const char *password){
    sp_error err;
    sp_session *sess;

    extern const uint8_t g_appkey[];
    extern const size_t g_appkey_size;

	config.api_version = SPOTIFY_API_VERSION;

	config.application_key_size = g_appkey_size;

	err = sp_session_create(&config, &sess);

    if(SP_ERROR_OK != err){
		fprintf(stderr, "failed to create session: %s\n",
		                sp_error_message(err));
		return 2;
    }

	err = sp_session_login(sess, username, password, 0, NULL);

	if (SP_ERROR_OK != err) {
		fprintf(stderr, "failed to login: %s\n",
		                sp_error_message(err));
		return 3;
    }

	g_session = sess;
	return 0;
}

int main(){
    int next_timeout = 0;
    char *username = "Scriptorius";
    char *pw;
    size_t len = 0;
    FILE *fp;

    fp = fopen("pass.txt", "r");
    if(fp == NULL)
        exit(1);

    // Password is first line
    getline(&pw, &len, fp);
    fclose(fp);

    // Strip newline
    pw[strlen(pw)-1] = '\0';

    pthread_mutex_init(&notify_mut, NULL);
    pthread_cond_init(&notify_cond, NULL);
    pthread_cond_init(&prompt_cond, NULL);

    if(spotify_init(username, pw) != 0){
		fprintf(stderr,"Spotify failed to initialize\n");
		exit(-1);
    }else{
        pthread_t id;
        can_prompt = 1;
        pthread_create(&id, NULL, promptloop, NULL);
    }

    pthread_mutex_lock(&notify_mut);
    for(;;){
        if(next_timeout == 0){
			while(!notify_events && !current_cmd)
				pthread_cond_wait(&notify_cond, &notify_mut);
        }else{
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);

            // Specify how much time to wait for
            ts.tv_sec += next_timeout / 1000;
            ts.tv_nsec += (next_timeout % 1000) * 1000000;

            while(!notify_events && !current_cmd){
                if(pthread_cond_timedwait(&notify_cond, &notify_mut, &ts))
                    break;
            }
        }

        if(current_cmd){
            command_t *cmd = current_cmd;
            current_cmd = NULL;
            pthread_mutex_unlock(&notify_mut);

            run_cmd(cmd);
            free(cmd);

            pthread_mutex_lock(&notify_mut);
        }

        notify_events = 0;
        pthread_mutex_unlock(&notify_mut);

        do{
            sp_session_process_events(g_session, &next_timeout);
        }while(next_timeout == 0);

        pthread_mutex_lock(&notify_mut);
    }
    return 0;
}
