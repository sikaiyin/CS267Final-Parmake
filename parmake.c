#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <omp.h>
#include "format.h"
#include "graph.h"
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include "set.h"


set *cycled_set;
set *task_set;
vector *task_vector;
graph *file_graph;
int new_changes_in_dependencies;
//pthread_mutex_t m;
//pthread_mutex_t n;

omp_lock_t *m;
omp_lock_t *n;

//pthread_cond_t cv;
//pthread_cond_t cu;

// return value: 1 means cycle detected, 0 means no cycle detected
int detect_cycle(char *curr_target) {
    if (set_contains(cycled_set, curr_target)) {
        return 1;
    }

    set_add(cycled_set, curr_target);

    vector *dependencies = graph_neighbors(file_graph, curr_target);
    for (size_t i = 0; i < vector_size(dependencies); i++) {
        char *next_target = (char *)vector_get(dependencies, i);
        int have_cycle = detect_cycle(next_target);
        if (have_cycle) {
            vector_destroy(dependencies);
            return 1;
        }
    }
    vector_destroy(dependencies);
    set_remove(cycled_set, curr_target);

    if (!set_contains(task_set, curr_target)) {
        rule_t *rule = (rule_t *)graph_get_vertex_value(file_graph, curr_target);
        rule->data = (void *)graph_vertex_degree(file_graph, curr_target);
        set_add(task_set, curr_target);
    }

    return 0;
}

void change_antineighbor_data(char *curr_target) {
    vector *curr_antineighbors = graph_antineighbors(file_graph, curr_target);
    omp_set_lock(&n[0]);
    for (size_t i = 0; i < vector_size(curr_antineighbors); i++) {
        char *curr_antineighbor = (char *)vector_get(curr_antineighbors, i);
        rule_t *curr_antineighbor_rule = (rule_t *)graph_get_vertex_value(file_graph, curr_antineighbor);
        curr_antineighbor_rule->data = (void *)(((size_t)(curr_antineighbor_rule->data)) - 1);
    }
    new_changes_in_dependencies = 1;
    //pthread_cond_broadcast(&cu);
    omp_unset_lock(&n[0]);
    vector_destroy(curr_antineighbors);
    free(curr_target);
}

// state: -1 failed, 0 untouched, 1 satisfied
void *run_makefile(void *ptr) {
    while (1) {
        int no_more_task = 0;
        int waiting_for_ready_target = 1;
        char *curr_target = NULL;

        while (waiting_for_ready_target) {
            omp_set_lock(&n[0]);
            if (vector_size(task_vector) == 0) {
                no_more_task = 1;
                new_changes_in_dependencies = 1;
                waiting_for_ready_target = 0;
            }
            else {
                size_t i = 0;
                for (; i < vector_size(task_vector); i++) {
                    char *temp_target = (char *)vector_get(task_vector, i);
                    if ((size_t)(((rule_t *)graph_get_vertex_value(file_graph, temp_target))->data) == 0) {
                        curr_target = strdup(temp_target);
                        waiting_for_ready_target = 0;
                        break;
                    }
                }

                if (!waiting_for_ready_target) {
                    vector_erase(task_vector, i);
                    new_changes_in_dependencies = 1;
                }
                else {
                    new_changes_in_dependencies = 0;
                }
            }
            //while (!new_changes_in_dependencies) pthread_cond_wait(&cu, &n[0]);
            omp_unset_lock(&n[0]);
        }

        if (no_more_task) break;

        rule_t *curr_rule = (rule_t *)graph_get_vertex_value(file_graph, curr_target);
        if (curr_rule->state != 0) {
            change_antineighbor_data(curr_target);
            continue;
        }

        vector *dependencies = graph_neighbors(file_graph, curr_target);

        struct stat curr_st;
        int is_file = access(curr_target, F_OK); // on success, return 0
        if (is_file == 0) stat(curr_target, &curr_st);

        int any_dependencies_failed = 0;
        int have_non_file_dependencies = 0;
        int have_newer_file_dependencies = 0;

        for (size_t i = 0; i < vector_size(dependencies); i++) {
            char *next_target = (char *)vector_get(dependencies, i);

            if (access(next_target, F_OK) != 0) {
                have_non_file_dependencies = 1;

                rule_t *next_rule = (rule_t *)graph_get_vertex_value(file_graph, next_target);
                omp_set_lock(&m[0]);
                //while (next_rule->state == 0) pthread_cond_wait(&cv, &m[0]);
                if (next_rule->state == -1) any_dependencies_failed = 1;
                omp_unset_lock(&m[0]);
            }
            else if (is_file == 0) {
                struct stat dependency_st;
                stat(next_target, &dependency_st);

                if (difftime(curr_st.st_mtime, dependency_st.st_mtime) < 0) {
                    have_newer_file_dependencies = 1;
                }
            }
        }

        vector_destroy(dependencies);

        if (any_dependencies_failed) {
            omp_set_lock(&m[0]);
            curr_rule->state = -1;
            //pthread_cond_broadcast(&cv);
            omp_unset_lock(&m[0]);

            change_antineighbor_data(curr_target);
            continue;
        }

        if (is_file == 0) {
            if (!have_non_file_dependencies && !have_newer_file_dependencies) {
                omp_set_lock(&m[0]);
                curr_rule->state = 1;
                //pthread_cond_broadcast(&cv);
                omp_unset_lock(&m[0]);

                change_antineighbor_data(curr_target);
                continue;
            }
        }

        int any_commands_failed = 0;
        for (size_t i = 0; i < vector_size(curr_rule->commands); i++) {
            if (system(vector_get(curr_rule->commands, i)) != 0) {
                any_commands_failed = 1;
                break;
            }
        }

        if (any_commands_failed) {
            omp_set_lock(&m[0]);
            curr_rule->state = -1;
            //pthread_cond_broadcast(&cv);
            omp_unset_lock(&m[0]);
        }
        else {
            omp_set_lock(&m[0]);
            curr_rule->state = 1;
            //pthread_cond_broadcast(&cv);
            omp_unset_lock(&m[0]);
        }
        change_antineighbor_data(curr_target);
    }

    return NULL;
}

void parallel_makefile(size_t num_threads) {
    // pthread_mutex_init(&m, NULL);
    // pthread_mutex_init(&n, NULL);
    m = (omp_lock_t *) malloc(1 * sizeof(omp_lock_t));
    n = (omp_lock_t *) malloc(1 * sizeof(omp_lock_t));
    omp_init_lock(&m[0]);
    omp_init_lock(&n[0]);
    //pthread_cond_init(&cv, NULL);
    //pthread_cond_init(&cu, NULL);

    //pthread_t pthread_id[num_threads];
    //for (size_t i = 0; i < num_threads; i++) {
    //    pthread_id[i] = i;
    //    pthread_create(&pthread_id[i], NULL, run_makefile, NULL);
    //}

    //void *result[num_threads];
    //for (size_t i = 0; i < num_threads; i++) {
    //    pthread_join(pthread_id[i], &result[i]);
    //}

#ifdef _OPENMP
#pragma omp parallel default(shared)
#endif
{
    run_makefile;
}

// pthread_mutex_destroy(&m);
// pthread_mutex_destroy(&n);
//pthread_cond_destroy(&cv);
//pthread_cond_destroy(&cu);
}

int parmake(char *makefile, size_t num_threads, char **targets) {
    // good luck!
    file_graph = parser_parse_makefile(makefile, targets);
    vector *build_targets = graph_neighbors(file_graph, "");

    for (size_t i = 0; i < vector_size(build_targets); i++) {
        char *curr_target = (char *)vector_get(build_targets, i);
        cycled_set = string_set_create();
        task_set = string_set_create();

        int have_cycle = detect_cycle(curr_target);
        if (!have_cycle) {
            task_vector = set_elements(task_set);

            parallel_makefile(num_threads);

            vector_destroy(task_vector);
        }
        else {
            print_cycle_failure(curr_target);
        }

        set_destroy(cycled_set);
        set_destroy(task_set);
    }

    vector_destroy(build_targets);
    graph_destroy(file_graph);
    return 0;
}
