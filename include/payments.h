#ifndef PAYMENTS_H
#define PAYMENTS_H

#include <stdint.h>
#include <gsl/gsl_rng.h>
#include "array.h"
#include "heap.h"
#include "cloth.h"
#include "network.h"
#include "routing.h"

enum payment_error_type{
  NOERROR,
  NOBALANCE,
  OFFLINENODE, //it corresponds to `FailUnknownNextPeer` in lnd
};

/* register an eventual error occurred when the payment traversed a hop */
struct payment_error{
  enum payment_error_type type;
  struct route_hop* hop;
};

struct payment {
  long id;
  long sender;
  long receiver;
  uint64_t amount; //millisatoshis
  uint64_t max_fee_limit; //millisatoshis
  struct route* route;
  uint64_t start_time;
  uint64_t end_time;
  int attempts;
  struct payment_error error;
  /* attributes for multi-path-payment (mpp)*/
  unsigned int is_shard;
  long shards_id[2];
  /* attributes used for computing stats */
  unsigned int is_success;
  int offline_node_count;
  int no_balance_count;
  unsigned int is_timeout;
  struct element* history; // list of `struct attempt`
  int is_path_changed; // 1 if the path has been changed, 0 otherwise
  double jaccard_index;
  double dice_index;
  double lcs_similarity; // Longest Common Subsequence similarity
  double ld_similarity;  //levenshtein distance similarity
  int is_estimate_success;
};

struct attempt {
  int attempts;
  uint64_t end_time;
  long error_edge_id;
  enum payment_error_type error_type;
  struct array* route; // array of `struct edge_snapshot`
  short is_succeeded;
};

// struct weighted_ {
//   double total_weight = 0.0;
//   double* weights;
// };

struct payment* new_payment(long id, long sender, long receiver, uint64_t amount, uint64_t start_time, uint64_t max_fee_limit);
struct array* initialize_payments(struct payments_params pay_params, struct network_params net_params, struct network *network, long n_nodes, gsl_rng* random_generator);
void compute_node_weights(struct network *network, long n_nodes, double alpha, double epsilon, double *weights, double *total_weight, unsigned int pattern);
long weighted_random_select(double* weights, double total_weight, gsl_rng* rng, long n_nodes);
void add_attempt_history(struct payment* pmt, struct network* network, uint64_t time, short is_succeeded);

#endif
