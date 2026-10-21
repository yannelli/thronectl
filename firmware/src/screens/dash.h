#pragma once
#include "../core/platform.h"

/** Detail slides after the Home overview, flipped with , and / or < and >. Slide 0 is the overview. */
namespace dash {
constexpr int COUNT = 6;
const char* title(int slide);
/** Draws the title row and the page dots, and returns the y where content starts. */
int titleRow(Canvas& c, const std::string& name, const std::string& right, int slide);
void draw(Canvas& c, int slide);
}  // namespace dash
