
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

static const char *home_children[]  = { "Working", "Cooking", "Cleaning", "Learning" };
static const char *relax_children[] = {"Phone", "Gaming", "Movies", "Reading"};
static const char *street_children[]   = { "Social", "Commute", "Sport", "Walking" };
static const char *body_children[] = { "Sleep", "Hygiene" };

static const TaskCategory task_categories[] = {
  { "Home",  home_children,  sizeof(home_children)  / sizeof(home_children[0]) },
  { "Street",   street_children,   sizeof(street_children)   / sizeof(street_children[0]) },
  { "Relax", relax_children, sizeof(relax_children) / sizeof(relax_children[0]) },
  { "Body", body_children, sizeof(body_children) / sizeof(body_children[0])}
};

static const int task_category_count = sizeof(task_categories) / sizeof(task_categories[0]);
