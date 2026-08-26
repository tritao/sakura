#ifndef SAKURA_CONTROL_TRANSPORT_H
#define SAKURA_CONTROL_TRANSPORT_H

#include "sakura-control-client.h"

#include "sakura-core.h"

typedef enum {
	SAKURA_CONTROL_REQUEST_NONE,
	SAKURA_CONTROL_REQUEST_GET_SNAPSHOT,
	SAKURA_CONTROL_REQUEST_CREATE_GROUP,
	SAKURA_CONTROL_REQUEST_CREATE_TASK,
	SAKURA_CONTROL_REQUEST_UPDATE_GROUP,
	SAKURA_CONTROL_REQUEST_SET_GROUP_ARCHIVED,
	SAKURA_CONTROL_REQUEST_DELETE_GROUP,
	SAKURA_CONTROL_REQUEST_UPDATE_TASK,
	SAKURA_CONTROL_REQUEST_SET_TASK_ARCHIVED,
	SAKURA_CONTROL_REQUEST_DELETE_TASK,
	SAKURA_CONTROL_REQUEST_CREATE_TERMINAL,
	SAKURA_CONTROL_REQUEST_CREATE_CODEX,
	SAKURA_CONTROL_REQUEST_TERMINAL_INPUT,
	SAKURA_CONTROL_REQUEST_TERMINAL_RESIZE,
	SAKURA_CONTROL_REQUEST_CLOSE_TERMINAL,
	SAKURA_CONTROL_REQUEST_SUBSCRIBE_EVENTS,
	SAKURA_CONTROL_REQUEST_ATTACH_TERMINAL,
	SAKURA_CONTROL_REQUEST_DETACH_TERMINAL,
	SAKURA_CONTROL_REQUEST_HELLO,
	SAKURA_CONTROL_REQUEST_RESTART_TERMINAL,
	SAKURA_CONTROL_REQUEST_UPDATE_PAGE,
	SAKURA_CONTROL_REQUEST_DELETE_PAGE,
	SAKURA_CONTROL_REQUEST_MOVE_GROUP,
	SAKURA_CONTROL_REQUEST_MOVE_TASK,
	SAKURA_CONTROL_REQUEST_MOVE_PAGE,
	SAKURA_CONTROL_REQUEST_RENAME_PAGE,
	SAKURA_CONTROL_REQUEST_SET_PAGE_ARCHIVED,
	SAKURA_CONTROL_REQUEST_SET_TASK_STATUS,
	SAKURA_CONTROL_REQUEST_LIST_FILES,
	SAKURA_CONTROL_REQUEST_READ_FILE,
	SAKURA_CONTROL_REQUEST_WRITE_FILE
} SakuraControlRequestKind;

typedef struct {
	gchar *path;
	gchar *name;
	gboolean directory;
	guint64 size;
	gint64 modified_unix;
	gboolean readonly;
	gchar *version;
} SakuraControlFileEntry;

typedef struct {
	gchar *request_id;
	SakuraControlRequestKind kind;
	gchar *parent_id;
	gchar *target_id;
	gchar *title;
	gchar *directory;
	gchar *group_id;
	gchar *task_id;
	gchar *page_id;
	gchar *terminal_id;
	gchar *cwd;
	gchar *reasoning_effort;
	gchar *resume_session_id;
	gchar *tracking_token;
	guint terminal_kind;
	guint order;
	gboolean has_order;
	gchar *provider;
	gchar *external_id;
	gchar *url;
	gchar *client_name;
	gchar *workspace_id;
	gchar *worktree_id;
	gchar *path;
	gchar *expected_file_version;
	gboolean archived;
	gboolean after;
	gboolean title_set_by_user;
	guint protocol_version;
	guint8 *input_data;
	gsize input_length;
	guint cols;
	guint rows;
	guint status;
	guint64 after_sequence;
	gboolean has_after_output_offset;
	guint64 after_output_offset;
	gboolean has_expected_revision;
	guint64 expected_revision;
	guint64 file_offset;
	guint64 file_length;
	gboolean has_file_length;
	guint8 *file_data;
	gsize file_data_length;
	gboolean truncate_file;
} SakuraControlRequest;

typedef struct {
	gchar *request_id;
	gboolean has_snapshot;
	gboolean has_error;
	gchar *error_code;
	gchar *error_message;
	gboolean error_retryable;
	guint64 error_current_revision;
	gboolean accepted;
	gchar *accepted_kind;
	gchar *accepted_id;
	gboolean hello;
	guint hello_protocol_version;
	gchar *agent_version;
	gchar *workspace_id;
	guint64 capabilities;
	gboolean attached;
	gchar *attached_terminal_id;
	guint attached_cols;
	guint attached_rows;
	guint attached_status;
	guint8 *attached_output;
	gsize attached_output_length;
	guint64 attached_output_start_offset;
	guint64 attached_output_end_offset;
	gboolean has_file_list;
	gchar *file_root_uri;
	gchar *file_version;
	GPtrArray *file_entries; /* SakuraControlFileEntry *, owned. */
	gboolean has_file_read;
	guint8 *file_data;
	gsize file_data_length;
	gboolean file_eof;
	gboolean has_file_write;
	guint64 workspace_revision;
} SakuraControlResponse;

typedef struct {
	const gchar *remote_code;
	gboolean retryable;
	guint64 current_revision;
} SakuraControlRemoteError;

void sakura_control_request_clear(SakuraControlRequest *request);
void sakura_control_response_clear(SakuraControlResponse *response);
gboolean sakura_control_response_get_remote_error(
	const SakuraControlResponse *response, SakuraControlRemoteError *remote_error);

gboolean sakura_control_frame_read(GInputStream *input,
	                                  GByteArray **payload,
	                                  GCancellable *cancellable,
	                                  GError **error);
gboolean sakura_control_frame_write(GOutputStream *output,
	                                   const guint8 *payload,
	                                   gsize payload_length,
	                                   GCancellable *cancellable,
	                                   GError **error);

gboolean sakura_control_encode_get_snapshot_request(const gchar *request_id,
	                                                  GByteArray *payload);
gboolean sakura_control_encode_list_files_request(
	const gchar *request_id, const gchar *worktree_id, const gchar *path,
	GByteArray *payload);
gboolean sakura_control_encode_read_file_request(
	const gchar *request_id, const gchar *worktree_id, const gchar *path,
	guint64 offset, guint64 length, gboolean has_length,
	const gchar *expected_version, GByteArray *payload);
gboolean sakura_control_encode_write_file_request(
	const gchar *request_id, const gchar *worktree_id, const gchar *path,
	const guint8 *data, gsize data_length, const gchar *expected_version,
	gboolean truncate, GByteArray *payload);
gboolean sakura_control_request_set_expected_revision(GByteArray *payload,
	                                                   guint64 revision);
gboolean sakura_control_encode_hello_request(const gchar *request_id,
	                                           guint protocol_version,
	                                           const gchar *client_name,
	                                           const gchar *workspace_id,
	                                           GByteArray *payload);
gboolean sakura_control_encode_create_group_request(
	const gchar *request_id, const gchar *parent_id, const gchar *title,
	const gchar *directory, GByteArray *payload);
gboolean sakura_control_encode_create_task_request(
	const gchar *request_id, const gchar *group_id, const gchar *parent_id,
	const gchar *title, const gchar *provider, const gchar *external_id,
	const gchar *url, GByteArray *payload);
gboolean sakura_control_encode_update_group_request(
	const gchar *request_id, const gchar *group_id, const gchar *title,
	const gchar *directory, GByteArray *payload);
gboolean sakura_control_encode_move_group_request(
	const gchar *request_id, const gchar *group_id, const gchar *parent_id,
	const gchar *target_id, gboolean after, GByteArray *payload);
gboolean sakura_control_encode_set_group_archived_request(
	const gchar *request_id, const gchar *group_id, gboolean archived,
	GByteArray *payload);
gboolean sakura_control_encode_delete_group_request(
	const gchar *request_id, const gchar *group_id, GByteArray *payload);
gboolean sakura_control_encode_update_task_request(
	const gchar *request_id, const gchar *task_id, const gchar *title,
	GByteArray *payload);
gboolean sakura_control_encode_move_task_request(
	const gchar *request_id, const gchar *task_id, const gchar *group_id,
	const gchar *parent_id, const gchar *target_id, gboolean after,
	GByteArray *payload);
gboolean sakura_control_encode_move_page_request(
	const gchar *request_id, const gchar *page_id, const gchar *group_id,
	const gchar *task_id, GByteArray *payload);
gboolean sakura_control_encode_rename_page_request(
	const gchar *request_id, const gchar *page_id, const gchar *title,
	gboolean title_set_by_user, GByteArray *payload);
gboolean sakura_control_encode_set_page_archived_request(
	const gchar *request_id, const gchar *page_id, gboolean archived,
	GByteArray *payload);
gboolean sakura_control_encode_set_task_status_request(
	const gchar *request_id, const gchar *task_id, guint status,
	GByteArray *payload);
gboolean sakura_control_encode_update_page_request(
	const gchar *request_id, const gchar *page_id, const gchar *group_id,
	const gchar *task_id, const gchar *title, gboolean title_set_by_user,
	gboolean archived, GByteArray *payload);
gboolean sakura_control_encode_delete_page_request(
	const gchar *request_id, const gchar *page_id, GByteArray *payload);
gboolean sakura_control_encode_set_task_archived_request(
	const gchar *request_id, const gchar *task_id, gboolean archived,
	GByteArray *payload);
gboolean sakura_control_encode_delete_task_request(
	const gchar *request_id, const gchar *task_id, GByteArray *payload);
gboolean sakura_control_encode_create_terminal_request(
	const gchar *request_id, const gchar *terminal_id, const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_create_terminal_request_with_page(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, GByteArray *payload);
gboolean sakura_control_encode_create_terminal_request_with_order(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, guint order, gboolean has_order,
	GByteArray *payload);
gboolean sakura_control_encode_create_codex_request(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, const gchar *reasoning_effort,
	const gchar *resume_session_id, GByteArray *payload);
gboolean sakura_control_encode_terminal_input_request(
	const gchar *request_id, const gchar *terminal_id, const guint8 *data,
	gsize data_length, GByteArray *payload);
gboolean sakura_control_encode_terminal_resize_request(
	const gchar *request_id, const gchar *terminal_id, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_close_terminal_request(
	const gchar *request_id, const gchar *terminal_id, GByteArray *payload);
gboolean sakura_control_encode_attach_terminal_request(
	const gchar *request_id, const gchar *terminal_id, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_attach_terminal_request_after_offset(
	const gchar *request_id, const gchar *terminal_id, guint cols, guint rows,
	guint64 after_output_offset, GByteArray *payload);
gboolean sakura_control_encode_detach_terminal_request(
	const gchar *request_id, const gchar *terminal_id, GByteArray *payload);
gboolean sakura_control_encode_restart_terminal_request(
	const gchar *request_id, const gchar *terminal_id, const gchar *group_id,
	const gchar *task_id, const gchar *cwd, guint cols, guint rows,
	GByteArray *payload);
gboolean sakura_control_encode_restart_terminal_request_with_page(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, GByteArray *payload);
gboolean sakura_control_encode_restart_terminal_request_with_spec(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, SakuraTabKind kind, const gchar *resume_session_id,
	const gchar *reasoning_effort, const gchar *tracking_token,
	GByteArray *payload);
gboolean sakura_control_encode_restart_terminal_request_with_order(
	const gchar *request_id, const gchar *terminal_id, const gchar *page_id,
	const gchar *group_id, const gchar *task_id, const gchar *cwd,
	guint cols, guint rows, SakuraTabKind kind, const gchar *resume_session_id,
	const gchar *reasoning_effort, const gchar *tracking_token,
	guint order, gboolean has_order, GByteArray *payload);
gboolean sakura_control_encode_subscribe_events_request(
	const gchar *request_id, guint64 after_sequence, GByteArray *payload);
gboolean sakura_control_decode_request(const guint8 *payload,
	                                     gsize payload_length,
	                                     SakuraControlRequest *request,
	                                     GError **error);
gboolean sakura_control_encode_snapshot_response(
	const gchar *request_id, guint64 sequence,
	const SakuraCoreWorkspace *workspace,
	GByteArray *payload);
gboolean sakura_control_encode_snapshot_response_with_revision(
	const gchar *request_id, guint64 sequence, guint64 workspace_revision,
	const SakuraCoreWorkspace *workspace, GByteArray *payload);
gboolean sakura_control_encode_error_response(const gchar *request_id,
	                                            const gchar *code,
	                                            const gchar *message,
	                                            GByteArray *payload);
gboolean sakura_control_encode_error_response_with_revision(
	const gchar *request_id, const gchar *code, const gchar *message,
	guint64 current_revision, gboolean retryable, GByteArray *payload);
gboolean sakura_control_encode_accepted_response(const gchar *request_id,
	                                               const gchar *kind,
	                                               const gchar *id,
	                                               GByteArray *payload);
gboolean sakura_control_encode_accepted_response_with_revision(
	const gchar *request_id, const gchar *kind, const gchar *id,
	guint64 workspace_revision, GByteArray *payload);
gboolean sakura_control_encode_hello_response(const gchar *request_id,
	                                    guint protocol_version,
	                                    const gchar *agent_version,
	                                    guint64 capabilities,
	                                    const gchar *workspace_id,
	                                    GByteArray *payload);
gboolean sakura_control_encode_file_list_response(
	const gchar *request_id, const gchar *root_uri, const gchar *version,
	const GPtrArray *entries, GByteArray *payload);
gboolean sakura_control_encode_file_read_response(
	const gchar *request_id, const gchar *data, gsize data_length,
	const gchar *version, gboolean eof, GByteArray *payload);
gboolean sakura_control_encode_file_write_response(
	const gchar *request_id, const gchar *version, GByteArray *payload);
gboolean sakura_control_encode_terminal_attachment_response(
	const gchar *request_id, const SakuraCoreTerminal *terminal,
	const guint8 *replay_data, gsize replay_data_length, GByteArray *payload);
gboolean sakura_control_encode_terminal_attachment_response_with_offsets(
	const gchar *request_id, const SakuraCoreTerminal *terminal,
	guint64 replay_start_offset, guint64 replay_end_offset,
	const guint8 *replay_data, gsize replay_data_length, GByteArray *payload);
gboolean sakura_control_encode_workspace_changed_event(
	guint64 sequence, const SakuraCoreWorkspace *workspace, GByteArray *payload);
gboolean sakura_control_encode_workspace_changed_event_with_revision(
	guint64 sequence, guint64 workspace_revision,
	const SakuraCoreWorkspace *workspace, GByteArray *payload);
gboolean sakura_control_encode_terminal_output_event(
	guint64 sequence, const gchar *terminal_id, const guint8 *data,
	gsize data_length, gboolean final_chunk, GByteArray *payload);
gboolean sakura_control_encode_terminal_output_event_with_offsets(
	guint64 sequence, const gchar *terminal_id, guint64 start_offset,
	guint64 end_offset, const guint8 *data, gsize data_length,
	gboolean final_chunk, GByteArray *payload);
gboolean sakura_control_encode_terminal_status_event(
	guint64 sequence, const gchar *terminal_id, guint status,
	const gchar *message, GByteArray *payload);
gboolean sakura_control_decode_response(const guint8 *payload,
	                                      gsize payload_length,
	                                      SakuraControlResponse *response,
	                                      GError **error);
gboolean sakura_control_decode_snapshot_response(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	SakuraSessionSnapshot **snapshot, GError **error);
gboolean sakura_control_decode_workspace_changed_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	SakuraSessionSnapshot **snapshot, GError **error);
gboolean sakura_control_decode_terminal_output_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	gchar **terminal_id, guint8 **data, gsize *data_length,
	gboolean *final_chunk, GError **error);
gboolean sakura_control_decode_terminal_output_event_with_offsets(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	gchar **terminal_id, guint64 *start_offset, guint64 *end_offset,
	guint8 **data, gsize *data_length, gboolean *final_chunk,
	GError **error);
gboolean sakura_control_decode_terminal_status_event(
	const guint8 *payload, gsize payload_length, guint64 *sequence,
	gchar **terminal_id, guint *status, gchar **message, GError **error);

#endif /* SAKURA_CONTROL_TRANSPORT_H */
