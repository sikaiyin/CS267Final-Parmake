/**
 * Parallel Make
 * CS 241 - Fall 2016
 */
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>
#include <unistd.h>
#include "parmake.h"
#include "parser.h"
#include "queue.h"
#include "vector.h"
#include <pthread.h>
#include "rule.h"

int targetcount = 0;
omp_lock_t *m;

Vector * v;
Vector * circular;
void *mycopy(void *elem) {
  return (void*)elem;
}

void mydes(void *elem) {
  free(elem);
  return;
}

queue_t * ruleq;

void parsed_new_target(rule_t* t) {
  if(t==NULL) {
    return;
  }
  queue_push(ruleq, (void*)t);//push all the rule_t to a queue
  Vector_append(v, (void*)t);
  targetcount++;
  return;
}

void * thread_function() {
  //*(int*)id = 1;
  while(1) {
    pthread_mutex_lock(&m[0]);
    if(targetcount<=0) {
      pthread_mutex_unlock(&m[0]);
      break;
    }
    pthread_mutex_unlock(&m[0]);
    rule_t * r = (rule_t*)queue_pull(ruleq);
    size_t y = 0;
    int shouldreturn = 0;
    for(y=0;y<Vector_size(circular);y++) {
      rule_t * wa = Vector_get(circular,y);
      if(strcmp(wa->target,r->target)==0) {
        shouldreturn = 1;
      }
    }
    if(r->state!=0) {
      queue_push(ruleq, (void*)r);
    }
    else if(shouldreturn == 1) {
      r->state = -1;
      pthread_mutex_lock(&m[0]);
      queue_push(ruleq, (void*)r);
      targetcount--;
      pthread_mutex_unlock(&m[0]);
    }
    else {
      if(Vector_size(r->dependencies)==0) {
        Vector* temp = r->commands;
	      if(access(r->target,F_OK)==0) {
	        r->state = 1;
          pthread_mutex_lock(&m[0]);
          queue_push(ruleq, (void*)r);
          targetcount--;
          pthread_mutex_unlock(&m[0]);
        }
	      else {
          size_t ts = Vector_size(temp);
          size_t i=0;
          int ret = 0;
          for(i=0;i<ts;i++) {
            if(Vector_get(temp, i)!=NULL) {
	            ret = system(Vector_get(temp, i));
	            if(ret!=0) {
	              break;
	            }
            }
          }
          if(ret!=0) {
	          r->state = -1;
            pthread_mutex_lock(&m[0]);
            queue_push(ruleq, (void*)r);
            targetcount--;
            pthread_mutex_unlock(&m[0]);
          }
          else {
	          r->state = 1;
            pthread_mutex_lock(&m[0]);
            queue_push(ruleq, (void*)r);
	          targetcount--;
            pthread_mutex_unlock(&m[0]);
          }
        }
      }

      else {
        size_t i=0;
        int invalid = 0;
        for(i=0;i<Vector_size(r->dependencies);i++) {
	        if(((rule_t*)Vector_get(r->dependencies,i))->state==-1) {
	          r->state = -1;
            pthread_mutex_lock(&m[0]);
            queue_push(ruleq, (void*)r);
	          targetcount--;
	          invalid = 1;
            pthread_mutex_unlock(&m[0]);
            break;
	        }
	        if(Vector_get(r->dependencies,i)!=NULL && ((rule_t*)Vector_get(r->dependencies,i))->state==0) {
	          invalid = 1;
            pthread_mutex_lock(&m[0]);
            queue_push(ruleq, (void*)r);//push back the rule since dependencies
            pthread_mutex_unlock(&m[0]);
            break;
	        }
        }

        if(invalid==0) {
	        Vector* temp = r->commands;
	        int needrun = 0;
	        if(access(r->target,F_OK)==0) {
            struct stat s;
            struct stat a;
            stat(r->target, &a);
            size_t i = 0;
            for(i=0;i<Vector_size(r->dependencies);i++) {
              rule_t * tt = (rule_t*)Vector_get(r->dependencies,i);
              stat(tt->target, &s);
	            int dependaccess = access(tt->target,F_OK);
              if(s.st_mtime - a.st_mtime > 0) {
                needrun = 1;
                break;
              }
	            if(dependaccess!=0) {
		            needrun = 1;
		            break;
	            }
            }
	          if(needrun==0) {
	            r->state = 1;
              pthread_mutex_lock(&m[0]);
              queue_push(ruleq, (void*)r);
              targetcount--;
              pthread_mutex_unlock(&m[0]);
            }
	        }
	        if(access(r->target,F_OK)!=0 || needrun == 1) {
	          size_t ts = Vector_size(temp);
	          size_t i=0;
	          int ret = 0;
	          for(i=0;i<ts;i++) {
	            if(Vector_get(temp, i)!=NULL) {
	              ret = system(Vector_get(temp, i));
	              if(ret!=0) {
	                break;
	              }
	            }
            }
	          if(ret!=0) {
	            r->state = -1;
              pthread_mutex_lock(&m[0]);
              queue_push(ruleq, (void*)r);
	            targetcount--;
              pthread_mutex_unlock(&m[0]);
            }
	          else {
	            r->state = 1;
              pthread_mutex_lock(&m[0]);
              queue_push(ruleq, (void*)r);
	            targetcount--;
              pthread_mutex_unlock(&m[0]);
            }
          }
        }
      }
    }
  }
  return NULL;
}

int parmake(int argc, char **argv) {
  int a=-1;
  char * fvalue = NULL;
  char * jvalue = NULL;
  int numthreads = 1;
  while((a=getopt(argc,argv,"f:j:")) != -1) {
    switch(a) {
      case 'f':
        fvalue = optarg;
        break;
      case 'j':
        jvalue = optarg;
	      numthreads = atoi(jvalue);
        break;
      default:
	      break;
    }
  }
  FILE * f;
  if(fvalue!=NULL) {
    f = fopen(fvalue,"r");
    if(f==NULL) {
      exit(-1);
    }
  }
  else {
    f = fopen("./makefile","r");
    if(f==NULL) {
      f = fopen("./Makefile","r");
      if(f==NULL) {
        exit(-1);
      }
      else {
        fvalue = "./Makefile";
      }
    }
    else {
      fvalue = "./makefile";
    }
  }
  if(jvalue==NULL) {
    numthreads = 1;
  }
  fclose(f);

  v = Vector_create(mycopy, mydes);
  circular = Vector_create(mycopy, mydes);
  char ** targets = argv+optind;
  pthread_t p[numthreads];
  ruleq = queue_create(-1, mycopy, mydes);
  if(targets==NULL) {
    parser_parse_makefile(fvalue,NULL,parsed_new_target);
  }
  else {
    parser_parse_makefile(fvalue,targets,parsed_new_target);
  }
  size_t j=0;
  /*for(j=0;j<Vector_size(v);j++) {
    //printf("the target in v is %s\n",((rule_t*)Vector_get(v, j))->target);
  }*/
  for(j=0;j<Vector_size(v);j++) {
    rule_t * temp = Vector_get(v, j);
    size_t k = 0;
    size_t z = 0;
    //printf("%s : ",temp->target);
    for(k=0;k<Vector_size(temp->dependencies);k++) {
      rule_t * temp1 = Vector_get(temp->dependencies,k);
      //printf(" %s ",temp1->target);
      for(z=0;z<Vector_size(temp1->dependencies);z++) {
        rule_t * temp2 = Vector_get(temp1->dependencies,z);
	//printf(" %s ",temp2->target);
        if(strcmp(temp->target,temp2->target)==0) {
	  Vector_append(circular, (void*)temp);
	  Vector_append(circular, (void*)temp1);
        }
      }
    }
    //printf("\n");
  }
  /*for(j=0;j<Vector_size(circular);j++) {
    printf("the target in circular is %s\n",((rule_t*)Vector_get(circular, j))->target);
  }*/
  int i=0;
  int * id = malloc(numthreads* sizeof(int));
  m = (omp_lock_t *)malloc(1 * sizeof(omp_lock_t));
  omp_init_lock(&m[0]);

//  for(i=0;i<numthreads;i++) {
//    id[i] = i+1;
//    pthread_create(&p[i], NULL, thread_function, (void*)&id[i]);
//  }
#ifdef _OPENMP
#pragma omp parallel default(shared)
#endif
  {
    thread_function();
  }
  //  for(i=0;i<numthreads;i++) {
  //    pthread_join(p[i],NULL);
  //  }
  queue_destroy(ruleq);
//  free(id);
  return 0;
}
