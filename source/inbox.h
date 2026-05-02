// Full-screen modal inbox viewer for orchestrator messages and team
// proposals. Mirrors output_viewer.c/h shape — caller hands us a base URL
// and a pre-parsed list of messages, we run the loop until the user
// dismisses (B from the list view).
//
// Why a modal: the inbox detail flow has nested screens (list -> message ->
// proposal team summary -> approve/reject confirm) that would balloon the
// existing canvas+detail.c surface. Keeping it self-contained here matches
// the pattern set by output_viewer and pin.

#ifndef COG_INBOX_H
#define COG_INBOX_H

#include "render.h"
#include <stdbool.h>

#define INBOX_MAX_MESSAGES   32
#define INBOX_MAX_PROP_AGENTS 12

typedef struct {
    char name[40];
    char cli[16];
    char model[24];
    char role[20];
} InboxProposalAgent;

typedef struct {
    char id[40];
    char agent_name[40];
    char message[256];
    char priority[12];     // "low" | "normal" | "high" | "urgent"
    char created_at[32];
    bool read;
    // Proposal fields — only set if proposal_id[0] != '\0'.
    char proposal_id[40];
    char proposal_summary[256];
    char proposal_status[16];   // "pending" | "approved" | "rejected" | "expired"
    InboxProposalAgent proposal_agents[INBOX_MAX_PROP_AGENTS];
    int proposal_agent_count;
} InboxMsg;

// Run the modal viewer. Returns true if the user took an action that
// changed state (mark-read, approve, reject) and the caller should bump
// the next-poll timer. base_url ends in /r/<token>/ — we append "inbox/...".
bool cog_inbox_run(CogRender *r, const char *base_url,
                   InboxMsg *msgs, int count);

#endif
