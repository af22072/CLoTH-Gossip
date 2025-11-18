#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include <inttypes.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_math.h>
#include "../include/array.h"
#include "../include/htlc.h"
#include "../include/payments.h"
#include "../include/network.h"

/* Functions in this file generate the payments that are exchanged in the payment-channel network during the simulation */


struct payment* new_payment(long id, long sender, long receiver, uint64_t amount, uint64_t start_time, uint64_t max_fee_limit) {
  struct payment * p;
  p = malloc(sizeof(struct payment));
  p->id=id;
  p->sender= sender;
  p->receiver = receiver;
  p->amount = amount;
  p->start_time = start_time;
  p->route = NULL;
  p->is_success = 0;
  p->offline_node_count = 0;
  p->no_balance_count = 0;
//  p->edge_occupied_count = 0;
  p->is_timeout = 0;
  p->end_time = 0;
  p->attempts = 0;
  p->error.type = NOERROR;
  p->error.hop = NULL;
  p->is_shard = 0;
  p->shards_id[0] = p->shards_id[1] = -1;
  p->history = NULL;
  p->max_fee_limit = max_fee_limit;
  p->is_path_changed = 0;
  p->jaccard_index = 1.0;
  p->dice_index = 1.0;
  p->lcs_similarity = 1.0;
  p->ld_similarity = 1.0;
  p->is_estimate_success = 0;
  return p;
}


/* generate random payments and store them in "payments.csv" */
void generate_random_payments(struct payments_params pay_params,struct network_params net_params, struct network *network, long n_nodes, gsl_rng * random_generator) {
  long i, sender_id, receiver_id;
  uint64_t  payment_amount=0, payment_time=1, next_payment_interval, max_fee_limit=UINT64_MAX;
  long payment_idIndex=0;
  FILE* payments_file;

  payments_file = fopen("payments.csv", "w");
  if(payments_file==NULL) {
    fprintf(stderr, "ERROR: cannot open file payments.csv\n");
    exit(-1);
  }
  fprintf(payments_file, "id,sender_id,receiver_id,amount,start_time,max_fee_limit\n");
  //ここから
  int n_patterns = 5; // 利用可能なパターン数
  double weights[n_patterns][n_nodes], total_weight[n_patterns];
  for (i = 0; i < n_patterns; i++) {
    total_weight[i] = 0.0;
  }
  if (net_params.weighted_random_select != 0) {
    // 重み配列の作成
    for (i = 0; i < n_patterns; i++) {
      compute_node_weights(network, n_nodes, 1.0, 1.0, weights[i], &total_weight[i], i+1);
    }
    //compute_node_weights(network, n_nodes, 1.0, 1.0, weights, &total_weight, net_params.weighted_random_select);
  }
  //gsl_rng_set(random_generator, 12345);
  long low_ids[pay_params.n_payments];
  long high_ids[pay_params.n_payments];
  for(i=0;i<pay_params.n_payments;i++) {
    do{
          low_ids[i] = weighted_random_select(weights[0], total_weight[0], random_generator, n_nodes);
          high_ids[i] = weighted_random_select(weights[2], total_weight[2], random_generator, n_nodes);
    } while(low_ids[i]==high_ids[i]);
  }
  for(i=0;i<pay_params.n_payments;i++) {
    do{
      if (net_params.weighted_random_select != 0) {
        if (net_params.weighted_random_select == 1) {
          sender_id = weighted_random_select(weights[0], total_weight[0], random_generator, n_nodes);
          receiver_id = weighted_random_select(weights[0], total_weight[0], random_generator, n_nodes);
        }
        else if (net_params.weighted_random_select == 2)  {
          sender_id = weighted_random_select(weights[2], total_weight[2], random_generator, n_nodes);
          receiver_id = weighted_random_select(weights[2], total_weight[2], random_generator, n_nodes);
        }
        else if (net_params.weighted_random_select == 3)  {
          sender_id = low_ids[i];
          receiver_id = high_ids[i];
        }
        else if (net_params.weighted_random_select == 4)  {
          sender_id = high_ids[i];;
          receiver_id = low_ids[i];;
        }
        //receiver_id = weighted_random_select(weights, total_weight, random_generator, n_nodes);
      }
      else {  // 通常の一様乱数選択
        sender_id = gsl_rng_uniform_int(random_generator,n_nodes);
        receiver_id = gsl_rng_uniform_int(random_generator, n_nodes);
      }
      //ここまで
      // sender_id = gsl_rng_uniform_int(random_generator,n_nodes);
      // receiver_id = gsl_rng_uniform_int(random_generator, n_nodes);
    } while(sender_id==receiver_id);
    payment_amount = fabs(pay_params.amount_mu + gsl_ran_ugaussian(random_generator) * pay_params.amount_sigma)*1000.0; // convert satoshi to millisatoshi
    /* payment interarrival time is an exponential (Poisson process) whose mean is the inverse of payment rate
       (expressed in payments per second, then multiplied to convert in milliseconds)
     */
    next_payment_interval = 1000*gsl_ran_exponential(random_generator, pay_params.inverse_payment_rate);
    payment_time += next_payment_interval;
    if(pay_params.max_fee_limit_sigma != -1 && pay_params.max_fee_limit_mu != -1) {
        max_fee_limit = fabs(pay_params.max_fee_limit_mu + gsl_ran_ugaussian(random_generator) * pay_params.max_fee_limit_sigma)*1000.0; // convert satoshi to millisatoshi
    }
    fprintf(payments_file, "%ld,%ld,%ld,%ld,%ld,%ld\n", payment_idIndex++, sender_id, receiver_id, payment_amount, payment_time, max_fee_limit);
  }

  fclose(payments_file);
}
// ノード次数に基づく重み配列と合計重みを計算
void compute_node_weights(struct network *network, long n_nodes, double alpha, double epsilon, double *weights, double *total_weight, unsigned int pattern) {
    long node_degrees[n_nodes];
    *total_weight = 0.0;
    for (long i = 0; i < n_nodes; i++) {
        struct node* node = array_get(network->nodes, i);
        node_degrees[i] = array_len(node->open_edges);
        if (i>=n_nodes-5/*newnode*/)continue;
        switch (pattern) {
          case 1:// pattern 1: 重みが次数の逆数に比例_low
            weights[i] = 1.0 / pow((double)node_degrees[i] + epsilon, alpha);
            break;
          case 2:// pattern 2: 重みが次数10以下のノードに1.0、その他に0.0を割り当て
            weights[i] = node_degrees[i] <= 10 ? 1.0 : 0.0;
            break;
          case 3:// pattern 3: 重みが次数に比例_high
            weights[i] = pow((double)node_degrees[i] + epsilon, alpha);
            break;
          case 4:// pattern 4: 重みが次数10超のノードに1.0、その他に0.0を割り当て
            weights[i] = node_degrees[i] > 10 ? 1.0 : 0.0;
            break;
          case 5:// pattern 5: 重みが次数10超のノードの逆数に比例
            weights[i] = 1.0 / pow(node_degrees[i] > 10 ? (double)node_degrees[i] + epsilon : epsilon, alpha);
            break;
          default:
            printf("ERROR: unknown pattern\n");
        }
       *total_weight += weights[i];
    }
}

// 重みに基づくノード選択関数
long weighted_random_select(double* weights, double total_weight, gsl_rng* rng, long n_nodes) {
  double r = gsl_rng_uniform(rng) * total_weight; //決定用の0~total_weightの一様乱数
  double current_weight = 0.0;
  //rがノードiに対応する累積重み区間に入っているかどうかを調べ、当てはまる値を返す
  for (long i = 0; i < n_nodes; i++) {
    current_weight += weights[i];
    if (r < current_weight) {
      return i;
    }
  }
  return -1;
}

/* generate payments from file */
struct array* generate_payments(struct payments_params pay_params) {
  struct payment* payment;
  char row[256], payments_filename[256];
  long id, sender, receiver;
  uint64_t amount, time, max_fee_limit;
  struct array* payments;
  FILE* payments_file;

  if(!(pay_params.payments_from_file))
    strcpy(payments_filename, "payments.csv");
  else
    strcpy(payments_filename, pay_params.payments_filename);

  payments_file = fopen(payments_filename, "r");
  if(payments_file==NULL) {
    printf("ERROR: cannot open file <%s>\n", payments_filename);
    exit(-1);
  }

  payments = array_initialize(1000);

  fgets(row, 256, payments_file);
  while(fgets(row, 256, payments_file) != NULL) {
    sscanf(row, "%ld,%ld,%ld,%"SCNu64",%"SCNu64",%"SCNu64"", &id, &sender, &receiver, &amount, &time, &max_fee_limit);
    payment = new_payment(id, sender, receiver, amount, time, max_fee_limit);
    payments = array_insert(payments, payment);
  }
  fclose(payments_file);

  return payments;
}


struct array* initialize_payments(struct payments_params pay_params, struct network_params net_params, struct network *network, long n_nodes, gsl_rng* random_generator) {
  if(!(pay_params.payments_from_file))
    generate_random_payments(pay_params, net_params, network, n_nodes, random_generator);
  return generate_payments(pay_params);
}

void add_attempt_history(struct payment* pmt, struct network* network, uint64_t time, short is_succeeded){
  struct attempt* attempt = malloc(sizeof(struct attempt));
  attempt->attempts = pmt->attempts;
  attempt->end_time = time;
  if(is_succeeded){
    attempt->error_edge_id = 0;
    attempt->error_type = NOERROR;
  }else{
    attempt->error_edge_id = pmt->error.hop->edge_id;
    attempt->error_type = pmt->error.type;
  }
  attempt->is_succeeded = is_succeeded;
  long route_len = array_len(pmt->route->route_hops);
  attempt->route = array_initialize(route_len);

  for(int i = 0; i < route_len; i++){
    struct route_hop* route_hop = array_get(pmt->route->route_hops, i);
    struct edge* edge = array_get(network->edges, route_hop->edge_id);
    short is_in_group = 0;
    if(edge->group != NULL) is_in_group = 1;
    attempt->route = array_insert(attempt->route, take_edge_snapshot(edge, route_hop->amount_to_forward, is_in_group, route_hop->group_cap));
  }

  pmt->history = push(pmt->history, attempt);
}