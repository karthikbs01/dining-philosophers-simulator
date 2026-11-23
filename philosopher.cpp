#include "philosopher.h"
#include "resource_manager.h"

void dine(ResourceManager& table, int id, int rounds) {
    for (int meal = 0; meal < rounds; ++meal) {
        if (!table.request(id) || !table.acquire(id) || !table.finishMeal(id)) return;
    }
}
