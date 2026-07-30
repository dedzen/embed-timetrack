
#pragma once

// Define your task categories and their nested children here.
// Each category is a top-level entry in the Time menu; selecting it opens
// a popup listing its children. Add/remove entries freely -- everything
// else in the app reads from this table.

struct TaskCategory {
  const char *name;
  const char **children;
  int child_count;
};

static const char *home_children[]  = {  "Cooking", "Cleaning" };
static const char *mind_children[] = {"Working", "University", "Reflection"};
static const char *relax_children[] = {"Phone", "Gaming", "Movies", "Reading"};
static const char *street_children[]   = { "Social", "Commute", "Sport", "Walking" };
static const char *body_children[] = { "Sleep", "Hygiene", "Eating" };
static const char *misc_children[] = {"Errand"};


static const TaskCategory task_categories[] = {
  { "Body", body_children, sizeof(body_children) / sizeof(body_children[0])},
  { "Mind",   mind_children,   sizeof(mind_children)   / sizeof(mind_children[0]) },
  { "Relax", relax_children, sizeof(relax_children) / sizeof(relax_children[0]) },
  { "Home",  home_children,  sizeof(home_children)  / sizeof(home_children[0]) },
  { "Street", street_children, sizeof(street_children) / sizeof(street_children[0]) },
  { "Misc", misc_children, sizeof(misc_children) / sizeof(misc_children[0]) },
};

static const int task_category_count = sizeof(task_categories) / sizeof(task_categories[0]);
