#ifndef NIF_CALL_H
#define NIF_CALL_H

#pragma once

#include <erl_nif.h>

#ifndef NIF_CALL_IMPLEMENTATION

struct CallbackNifRes;
static CallbackNifRes * prepare_nif_call(ErlNifEnv* env);
static ERL_NIF_TERM nif_call(ErlNifEnv* caller_env, ErlNifPid evaluator, ERL_NIF_TERM fun, ERL_NIF_TERM args);
static ERL_NIF_TERM nif_call_evaluated(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
static void destruct_nif_call_res(ErlNifEnv *, void *obj);

#else

struct CallbackNifRes {
  static ErlNifResourceType *type;
  static ERL_NIF_TERM kAtomNil;
  static ERL_NIF_TERM kAtomENOMEM;

  ErlNifEnv * msg_env;
  ErlNifMutex *mtx = NULL;
  ErlNifCond *cond = NULL;
  ERL_NIF_TERM return_value;
};

ErlNifResourceType * CallbackNifRes::type = NULL;
ERL_NIF_TERM CallbackNifRes::kAtomNil;
ERL_NIF_TERM CallbackNifRes::kAtomENOMEM;

CallbackNifRes * prepare_nif_call(ErlNifEnv* env) {
  CallbackNifRes *res = (CallbackNifRes *)enif_alloc_resource(CallbackNifRes::type, sizeof(CallbackNifRes));
  if (!res) return NULL;
  memset(res, 0, sizeof(CallbackNifRes));

  res->msg_env = enif_alloc_env();
  if (!res->msg_env) {
    enif_release_resource(res);
    return NULL;
  }

  res->mtx = enif_mutex_create((char *)"nif_call_mutex");
  if (!res->mtx) {
    enif_free_env(res->msg_env);
    enif_release_resource(res);
    return NULL;
  }

  res->cond = enif_cond_create((char *)"nif_call_cond");
  if (!res->cond) {
    enif_free_env(res->msg_env);
    enif_mutex_destroy(res->mtx);
    enif_release_resource(res);
    return NULL;
  }

  res->return_value = CallbackNifRes::kAtomNil;

  return res;
}

static ERL_NIF_TERM nif_call(ErlNifEnv* caller_env, ErlNifPid evaluator, ERL_NIF_TERM fun, ERL_NIF_TERM args) {
  CallbackNifRes *callback_res = prepare_nif_call(caller_env);
  if (!callback_res) return CallbackNifRes::kAtomENOMEM;

  ERL_NIF_TERM callback_term = enif_make_resource(caller_env, (void *)callback_res);
  enif_send(caller_env, &evaluator, callback_res->msg_env, enif_make_copy(callback_res->msg_env, enif_make_tuple3(caller_env,
    fun,
    args,
    callback_term
  )));

  enif_cond_wait(callback_res->cond, callback_res->mtx);
  
  ERL_NIF_TERM return_value = enif_make_copy(caller_env, callback_res->return_value);
  enif_release_resource(callback_res);
  
  return return_value;
}

static ERL_NIF_TERM nif_call_evaluated(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  CallbackNifRes *res = NULL;
  if (!enif_get_resource(env, argv[0], CallbackNifRes::type, (void **)&res)) return enif_make_badarg(env);

  res->return_value = enif_make_copy(res->msg_env, argv[1]);
  enif_cond_signal(res->cond);

  return enif_make_atom(env, "ok");
}

static void destruct_nif_call_res(ErlNifEnv *, void *obj) {
  CallbackNifRes *res = (CallbackNifRes *)obj;
  if (res->cond) {
    enif_cond_destroy(res->cond);
    res->cond = NULL;
  }
  if (res->mtx) {
    enif_mutex_destroy(res->mtx);
    res->mtx = NULL;
  }
  if (res->msg_env) {
    enif_free_env(res->msg_env);
    res->msg_env = NULL;
  }
}

#endif

#endif  // NIF_CALL_H