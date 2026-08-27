#include "sakura-control-client.h"

int
main(void)
{
	SakuraControlTerminalAttachment attachment = { 0 };
	SakuraControlClientEvent event = { 0 };
	GError *error = NULL;
	gchar *terminal_id = NULL;

	if (SAKURA_CONTROL_CLIENT_API_VERSION != 1)
		return 1;
	sakura_control_terminal_attachment_clear(&attachment);
	sakura_control_client_event_clear(&event);

	if (sakura_control_client_create_terminal(
		    NULL, NULL, NULL, NULL, NULL, 80, 24, &terminal_id, &error) ||
	    error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_create_terminal_with_page(
		    NULL, NULL, "page", NULL, NULL, NULL, 80, 24, &terminal_id,
		    &error) || error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_create_codex_terminal(
		    NULL, NULL, NULL, NULL, NULL, NULL, 80, 24, NULL, NULL,
		    &terminal_id, &error) || error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_create_codex_terminal_with_model(
		    NULL, NULL, NULL, NULL, NULL, NULL, 80, 24,
		    "gpt-5.6-luna", "xhigh", NULL, &terminal_id, &error) ||
	    error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_create_codex_terminal_with_identity(
		    NULL, NULL, NULL, NULL, NULL, NULL, 80, 24,
		    "gpt-5.6-luna", "xhigh", NULL, "collision", &terminal_id,
		    &error) || error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_delete_page(NULL, "page", &error) ||
	    error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_terminal_input(
		    NULL, "terminal", (const guint8 *)"x", 1, &error) ||
	    error == NULL)
		return 1;
	g_clear_error(&error);
	if (sakura_control_client_read_event(NULL, &event, NULL, &error) ||
	    error == NULL)
		return 1;
	g_clear_error(&error);
	sakura_control_client_event_clear(&event);
	sakura_control_terminal_attachment_clear(&attachment);
	return 0;
}
