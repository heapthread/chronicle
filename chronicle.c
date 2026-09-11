#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wincred.h>
#include <git2.h>

#pragma comment(lib, "git2.lib")
#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Credui.lib")

void check_git_error(int error, const char *action) {
    if (error < 0) {
        const git_error *e = git_error_last();
        fprintf(stderr, "Error %s: %s (code %d)\n", action, (e && e->message) ? e->message : "unknown", error);
        git_libgit2_shutdown();
        exit(1);
    }
}

int get_windows_git_credentials(const char *url, char *user_out, char *pass_out, size_t max_len) {
    CREDENTIALA *cred = NULL;
    char target_git[512] = {0};
    char target_raw[512] = {0};

    const char *domain = strstr(url, "://");
    domain = domain ? domain + 3 : url;

    snprintf(target_git, sizeof(target_git), "git:%s", url);
    snprintf(target_raw, sizeof(target_raw), "https://%s", domain);

    const char *targets[] = { target_git, target_raw, "git:https://codeberg.org", "git:https://github.com", NULL };

    for (int i = 0; targets[i] != NULL; i++) {
        if (CredReadA(targets[i], CRED_TYPE_GENERIC, 0, &cred)) {
            if (cred->UserName && cred->CredentialBlob) {
                strncpy(user_out, cred->UserName, max_len - 1);
                size_t blob_size = cred->CredentialBlobSize < max_len - 1 ? cred->CredentialBlobSize : max_len - 1;
                memcpy(pass_out, (char *)cred->CredentialBlob, blob_size);
                pass_out[blob_size] = '\0';
                CredFree(cred);
                return 1;
            }
            CredFree(cred);
        }
    }
    return 0;
}

void save_windows_git_credentials(const char *url, const char *user, const char *pass) {
    char target_git[512] = {0};
    const char *domain = strstr(url, "://");
    domain = domain ? domain + 3 : url;
    snprintf(target_git, sizeof(target_git), "git:https://%s", domain);

    CREDENTIALA cred = {0};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = target_git;
    cred.UserName = (char *)user;
    cred.CredentialBlobSize = (DWORD)strlen(pass);
    cred.CredentialBlob = (LPBYTE)pass;
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;

    CredWriteA(&cred, 0);
}

int credentials_cb(
    git_cred **out,
    const char *url,
    const char *username_from_url,
    unsigned int allowed_types,
    void *payload)
{
    (void)payload;

    if (allowed_types & GIT_CREDTYPE_USERPASS_PLAINTEXT) {
        char user[256] = {0};
        char pass[256] = {0};

        if (get_windows_git_credentials(url, user, pass, sizeof(user))) {
            return git_cred_userpass_plaintext_new(out, user, pass);
        }

        const char *token = getenv("CODEBERG_TOKEN");
        if (!token) token = getenv("GITHUB_TOKEN");
        if (!token) token = getenv("GIT_TOKEN");

        if (token) {
            const char *username = (username_from_url && strlen(username_from_url) > 0) ? username_from_url : "oauth2";
            return git_cred_userpass_plaintext_new(out, username, token);
        }

        printf("\n--- Authentication Required ---\n");
        printf("Username: ");
        if (fgets(user, sizeof(user), stdin)) {
            user[strcspn(user, "\r\n")] = 0;
        }

        printf("Password or Personal Access Token: ");
        if (fgets(pass, sizeof(pass), stdin)) {
            pass[strcspn(pass, "\r\n")] = 0;
        }
        printf("-------------------------------\n");

        if (strlen(user) > 0 && strlen(pass) > 0) {
            save_windows_git_credentials(url, user, pass);
            return git_cred_userpass_plaintext_new(out, user, pass);
        }
    }

    if (allowed_types & GIT_CREDTYPE_SSH_KEY) {
        return git_cred_ssh_key_from_agent(out, username_from_url);
    }

    if (allowed_types & GIT_CREDTYPE_SSH_KEY) {
        const char *user = (username_from_url && strlen(username_from_url) > 0) ? username_from_url : "git";
        return git_cred_ssh_key_default_new(out, user);
    }

    return git_cred_default_new(out);
}

void cmd_init() {
    git_repository *repo = NULL;
    check_git_error(git_repository_init(&repo, ".", 0), "initializing repository");
    printf("✓ Initialized empty Chronicle repository in current directory.\n");
    printf("Active timeline: main\n");
    git_repository_free(repo);
}

void cmd_save(const char *message) {
    git_repository *repo = NULL;
    git_index *index = NULL;
    git_oid tree_oid, commit_oid;
    git_tree *tree = NULL;
    git_signature *sig = NULL;
    git_commit *parent = NULL;
    
    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_repository_index(&index, repo), "getting index");

    git_strarray paths = {0};
    check_git_error(git_index_add_all(index, &paths, GIT_INDEX_ADD_DEFAULT, NULL, NULL), "staging files");
    check_git_error(git_index_write(index), "writing index");
    check_git_error(git_index_write_tree(&tree_oid, index), "writing tree");
    check_git_error(git_tree_lookup(&tree, repo, &tree_oid), "looking up tree");

    if (git_signature_default(&sig, repo) < 0) {
        check_git_error(git_signature_now(&sig, "Chronicle User", "user@chronicle.local"), "creating default signature");
    }

    int parent_count = 0;
    git_reference *head_ref = NULL;
    if (git_repository_head(&head_ref, repo) == 0) {
        check_git_error(git_reference_peel((git_object **)&parent, head_ref, GIT_OBJECT_COMMIT), "peeling HEAD");
        parent_count = 1;
    }

    const git_commit *parents[] = { parent };

    check_git_error(git_commit_create(
        &commit_oid, repo, "HEAD", sig, sig,
        NULL, message, tree, parent_count, parents
    ), "creating checkpoint");

    char short_hash[8] = {0};
    git_oid_tostr(short_hash, sizeof(short_hash), &commit_oid);
    printf("✓ Saved Checkpoint: %s (\"%s\")\n", short_hash, message);

    if (head_ref) git_reference_free(head_ref);
    if (parent) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
    git_repository_free(repo);
}

void cmd_history() {
    git_repository *repo = NULL;
    git_revwalk *walker = NULL;
    git_oid oid;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    if (git_repository_head_unborn(repo)) {
        printf("No checkpoints saved yet.\n");
        git_repository_free(repo);
        return;
    }

    check_git_error(git_revwalk_new(&walker, repo), "creating revision walker");
    git_revwalk_sorting(walker, GIT_SORT_TIME);
    check_git_error(git_revwalk_push_head(walker), "pushing HEAD to walker");

    int count = 0;
    while (git_revwalk_next(&oid, walker) == 0 && count < 10) {
        git_commit *commit = NULL;
        check_git_error(git_commit_lookup(&commit, repo, &oid), "looking up commit");

        char short_hash[8] = {0};
        git_oid_tostr(short_hash, sizeof(short_hash), &oid);
        
        printf("  ● %s — %s (by %s)\n", short_hash, git_commit_summary(commit), git_commit_author(commit)->name);
        git_commit_free(commit);
        count++;
    }

    git_revwalk_free(walker);
    git_repository_free(repo);
}

void cmd_status() {
    git_repository *repo = NULL;
    git_status_list *status = NULL;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_status_list_new(&status, repo, &opts), "getting repository status");

    size_t count = git_status_list_entrycount(status);
    if (count == 0) {
        printf("✓ Everything is clean! No draft changes.\n");
    } else {
        printf("Draft Changes (Unsaved edits):\n");
        for (size_t i = 0; i < count; i++) {
            const git_status_entry *entry = git_status_byindex(status, i);
            if (entry->status & GIT_STATUS_WT_NEW)
                printf("  [+] New File:      %s\n", entry->index_to_workdir->old_file.path);
            else if (entry->status & GIT_STATUS_WT_MODIFIED)
                printf("  [*] Modified File: %s\n", entry->index_to_workdir->old_file.path);
            else if (entry->status & GIT_STATUS_WT_DELETED)
                printf("  [-] Deleted File:  %s\n", entry->index_to_workdir->old_file.path);
        }
    }

    git_status_list_free(status);
    git_repository_free(repo);
}

void cmd_discard() {
    git_repository *repo = NULL;
    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_checkout_head(repo, &opts), "discarding draft edits");
    printf("↺ Discarded all draft edits. Restored files to last saved checkpoint.\n");
    git_repository_free(repo);
}

int diff_print_callback(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload) {
    (void)delta; (void)hunk; (void)payload;
    fwrite(line->content, 1, line->content_len, stdout);
    return 0;
}

void cmd_diff() {
    git_repository *repo = NULL;
    git_diff *diff = NULL;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_diff_index_to_workdir(&diff, repo, NULL, NULL), "generating diff");
    check_git_error(git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, diff_print_callback, NULL), "printing diff");

    git_diff_free(diff);
    git_repository_free(repo);
}

void cmd_timeline_switch(const char *name) {
    git_repository *repo = NULL;
    git_reference *branch_ref = NULL;
    git_object *target_commit = NULL;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    
    int error = git_branch_lookup(&branch_ref, repo, name, GIT_BRANCH_LOCAL);
    if (error == GIT_ENOTFOUND) {
        check_git_error(git_revparse_single(&target_commit, repo, "HEAD"), "getting HEAD");
        check_git_error(git_branch_create(&branch_ref, repo, name, (git_commit *)target_commit, 0), "creating timeline");
        printf("Created timeline '%s'\n", name);
    }

    check_git_error(git_repository_set_head(repo, git_reference_name(branch_ref)), "setting HEAD");
    
    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    check_git_error(git_checkout_head(repo, &opts), "checking out timeline");

    printf("✓ Switched to timeline: %s\n", name);

    if (target_commit) git_object_free(target_commit);
    if (branch_ref) git_reference_free(branch_ref);
    git_repository_free(repo);
}

void create_merge_commit(git_repository *repo, git_annotated_commit *remote_commit, const char *branch_name) {
    git_oid tree_oid, commit_oid;
    git_tree *tree = NULL;
    git_index *index = NULL;
    git_signature *sig = NULL;
    git_commit *head_commit = NULL, *remote_commit_obj = NULL;
    git_reference *head_ref = NULL;

    check_git_error(git_repository_index(&index, repo), "getting index");
    check_git_error(git_index_write_tree(&tree_oid, index), "writing tree");
    check_git_error(git_tree_lookup(&tree, repo, &tree_oid), "looking up tree");

    check_git_error(git_repository_head(&head_ref, repo), "getting HEAD");
    check_git_error(git_reference_peel((git_object **)&head_commit, head_ref, GIT_OBJECT_COMMIT), "peeling HEAD");
    check_git_error(git_commit_lookup(&remote_commit_obj, repo, git_annotated_commit_id(remote_commit)), "looking up remote commit");

    if (git_signature_default(&sig, repo) < 0) {
        git_signature_now(&sig, "Chronicle User", "user@chronicle.local");
    }

    const git_commit *parents[] = { head_commit, remote_commit_obj };
    char msg[256];
    snprintf(msg, sizeof(msg), "Merge remote timeline into %s", branch_name);

    check_git_error(git_commit_create(&commit_oid, repo, "HEAD", sig, sig, NULL, msg, tree, 2, parents), "creating merge commit");
    git_repository_state_cleanup(repo);

    if (sig) git_signature_free(sig);
    if (tree) git_tree_free(tree);
    if (index) git_index_free(index);
    if (head_commit) git_commit_free(head_commit);
    if (remote_commit_obj) git_commit_free(remote_commit_obj);
    if (head_ref) git_reference_free(head_ref);
}

void cmd_sync(const char *remote_name) {
    git_repository *repo = NULL;
    git_remote *remote = NULL;
    git_reference *head_ref = NULL;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_repository_head(&head_ref, repo), "getting HEAD branch");
    
    const char *branch_name = git_reference_shorthand(head_ref);
    printf("↓ Syncing timeline '%s' with remote '%s'...\n", branch_name, remote_name);

    if (git_remote_lookup(&remote, repo, remote_name) < 0) {
        printf("Error: Remote '%s' is not configured.\n", remote_name);
        goto cleanup;
    }

    printf("→ Fetching latest changes from remote...\n");
    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    fetch_opts.callbacks.credentials = credentials_cb;
    if (git_remote_fetch(remote, NULL, &fetch_opts, NULL) < 0) {
        const git_error *e = git_error_last();
        printf("Warning: Fetching failed (%s). Attempting push...\n", (e && e->message) ? e->message : "unknown");
    }

    git_annotated_commit *fetch_head = NULL;
    git_reference *fetch_ref = NULL;
    if (git_reference_lookup(&fetch_ref, repo, "FETCH_HEAD") == 0) {
        if (git_annotated_commit_from_ref(&fetch_head, repo, fetch_ref) == 0) {
            git_merge_analysis_t analysis;
            git_merge_preference_t preference;
            git_merge_analysis(&analysis, &preference, repo, (const git_annotated_commit **)&fetch_head, 1);

            if (analysis & GIT_MERGE_ANALYSIS_NORMAL) {
                git_merge_options merge_opts = GIT_MERGE_OPTIONS_INIT;
                git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
                checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;

                git_merge(repo, (const git_annotated_commit **)&fetch_head, 1, &merge_opts, &checkout_opts);
                create_merge_commit(repo, fetch_head, branch_name);
                printf("✓ Automatically merged remote changes.\n");
            }
            git_annotated_commit_free(fetch_head);
        }
        git_reference_free(fetch_ref);
    }

    char refspec[256];
    snprintf(refspec, sizeof(refspec), "refs/heads/%s:refs/heads/%s", branch_name, branch_name);
    const char *push_refspecs[] = { refspec };
    git_strarray array = { (char **)push_refspecs, 1 };

    git_push_options options = GIT_PUSH_OPTIONS_INIT;
    options.callbacks.credentials = credentials_cb;

    int error = git_remote_push(remote, &array, &options);
    if (error == 0) {
        printf("✓ Sync complete! Uploaded timeline '%s' to remote '%s'.\n", branch_name, remote_name);
    } else {
        const git_error *e = git_error_last();
        printf("Error syncing with remote: %s\n", (e && e->message) ? e->message : "Authentication or conflict error");
    }

cleanup:
    if (remote) git_remote_free(remote);
    if (head_ref) git_reference_free(head_ref);
    git_repository_free(repo);
}

void cmd_undo() {
    git_repository *repo = NULL;
    git_reflog *reflog = NULL;
    git_reference *head_ref = NULL;

    check_git_error(git_repository_open(&repo, "."), "opening repository");
    check_git_error(git_reference_lookup(&head_ref, repo, "HEAD"), "getting HEAD reference");
    check_git_error(git_reflog_read(&reflog, repo, "HEAD"), "reading reflog");

    if (git_reflog_entrycount(reflog) < 2) {
        printf("Nothing to undo.\n");
        goto cleanup;
    }

    const git_reflog_entry *entry = git_reflog_entry_byindex(reflog, 1);
    const git_oid *prev_oid = git_reflog_entry_id_new(entry);

    git_object *target_obj = NULL;
    check_git_error(git_object_lookup(&target_obj, repo, prev_oid, GIT_OBJECT_COMMIT), "looking up commit");
    check_git_error(git_reset(repo, target_obj, GIT_RESET_HARD, NULL), "resetting state");

    char short_hash[8] = {0};
    git_oid_tostr(short_hash, sizeof(short_hash), prev_oid);
    printf("↺ Restored repository state to: %s\n", short_hash);

    git_object_free(target_obj);

cleanup:
    git_reflog_free(reflog);
    git_reference_free(head_ref);
    git_repository_free(repo);
}

int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    git_libgit2_init();

    if (argc < 2) {
        printf("Usage: chronicle <init|save|history|status|discard|diff|timeline|sync|undo> [args]\n");
        git_libgit2_shutdown();
        return 0;
    }

    if (strcmp(argv[1], "init") == 0) cmd_init();
    else if (strcmp(argv[1], "save") == 0) (argc < 3) ? printf("Usage: chronicle save \"message\"\n") : cmd_save(argv[2]);
    else if (strcmp(argv[1], "history") == 0) cmd_history();
    else if (strcmp(argv[1], "status") == 0) cmd_status();
    else if (strcmp(argv[1], "discard") == 0) cmd_discard();
    else if (strcmp(argv[1], "diff") == 0) cmd_diff();
    else if (strcmp(argv[1], "timeline") == 0) (argc < 3) ? printf("Usage: chronicle timeline <name>\n") : cmd_timeline_switch(argv[2]);
    else if (strcmp(argv[1], "sync") == 0) cmd_sync(argc > 2 ? argv[2] : "origin");
    else if (strcmp(argv[1], "undo") == 0) cmd_undo();
    else printf("Unknown command: %s\n", argv[1]);

    git_libgit2_shutdown();
    return 0;
}