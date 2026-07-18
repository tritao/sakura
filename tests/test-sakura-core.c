#include <glib.h>

#include "sakura-core.h"

static void
test_snapshot_defaults_and_ownership(void)
{
	SakuraSessionSnapshot *snapshot = sakura_session_snapshot_new();
	SakuraSessionGroupRecord *group = g_new0(SakuraSessionGroupRecord, 1);

	g_assert_nonnull(snapshot);
	g_assert_nonnull(snapshot->groups);
	g_assert_nonnull(snapshot->tasks);
	g_assert_nonnull(snapshot->tabs);
	g_assert_nonnull(snapshot->pages);
	g_assert_nonnull(snapshot->layouts);
	g_assert_cmpint(snapshot->selected_terminal, ==, -1);
	g_assert_cmpstr(snapshot->active_group_id, ==, "root");
	g_assert_true(snapshot->sidebar_visible);
	g_assert_cmpint(snapshot->sidebar_width, ==, 200);
	g_assert_false(snapshot->show_archived);

	group->id = g_strdup("group-a");
	group->title = g_strdup("A");
	g_ptr_array_add(snapshot->groups, group);
	g_assert_cmpuint(snapshot->groups->len, ==, 1);

	sakura_session_snapshot_free(snapshot);
}


int
main(int argc, char **argv)
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/core/snapshot/defaults-and-ownership",
	                test_snapshot_defaults_and_ownership);
	return g_test_run();
}
