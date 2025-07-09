#include <lwip/apps/ptpd.h>

void
servo_init_clock(ptp_clock_t* clock)
{
  DBG("servo_init_clock\n");

  clock->time_ms.seconds = 0;
  clock->time_ms.nanoseconds = 0;
  clock->observed_drift = 0;

  clock->owd_filt.n = 0;
  clock->owd_filt.s = clock->servo.s_delay;

  clock->ofm_filt.n = 0;
  clock->ofm_filt.s = clock->servo.s_offset;

#if PTPD_DEFAULT_PARENTS_STATS == 1
  clock->slv_filt.n = 0;
  clock->slv_filt.s = 6;
  clock->offset_history[0] = 0;
  clock->offset_history[1] = 0;
#endif

  clock->waiting_for_followup = FALSE;
  clock->waiting_for_pdelay_resp_followup = FALSE;

  clock->pdelay_t1.seconds = 0;
  clock->pdelay_t1.nanoseconds = 0;
  clock->pdelay_t2.seconds = 0;
  clock->pdelay_t2.nanoseconds = 0;
  clock->pdelay_t3.seconds = 0;
  clock->pdelay_t3.nanoseconds = 0;
  clock->pdelay_t4.seconds = 0;
  clock->pdelay_t4.nanoseconds = 0;

  clock->parent_ds.parent_stats = FALSE;
  clock->parent_ds.observed_parent_clock_phase_change_rate = 0;
  clock->parent_ds.observed_parent_offset_scaled_log_variance = 0;

  if (!clock->servo.no_adjust)
    ptpd_adj_frequency(0);

  ptpd_empty_event_queue(&clock->net_path);
}

static int32_t order(int32_t n)
{
  if (n < 0) n = -n;
  if (n == 0) return 0;
  return ptp_floor_log2(n);
}

static void filter(int32_t *nsec_current, Filter *filt)
{
  int32_t s, s2;
  filt->n++;
  if (filt->n == 1) {
    filt->y_prev = *nsec_current;
    filt->y_sum = *nsec_current;
    filt->s_prev = 0;
  }

  s = filt->s;
  if ((1 << s) > filt->n) s = order(filt->n);
  else filt->n = 1 << s;

  s2 = 30 - order(max(filt->y_prev, *nsec_current));
  s = min(s, s2);

  if (filt->s_prev > s) filt->y_sum >>= (filt->s_prev - s);
  else if (filt->s_prev < s) filt->y_sum <<= (s - filt->s_prev);

  filt->y_sum += *nsec_current - filt->y_prev;
  filt->y_prev = filt->y_sum >> s;
  filt->s_prev = s;

  DBGV("filter: %d -> %d (%d)\n", *nsec_current, filt->y_prev, s);
  *nsec_current = filt->y_prev;
}

void
servo_update_offset(ptp_clock_t* clock, const time_interval_t* sync_event_ingress_timestamp,
                     const time_interval_t* precise_origin_timestamp, const time_interval_t* correction_field)
{
  DBGV("servo_update_offset\n");
  ptp_sub_time(&clock->time_ms, sync_event_ingress_timestamp, precise_origin_timestamp);
  ptp_sub_time(&clock->time_ms, &clock->time_ms, correction_field);

  clock->current_ds.offset_from_master = clock->time_ms;

  switch (clock->port_ds.delay_mechanism) {
    case E2E:
      ptp_sub_time(&clock->current_ds.offset_from_master,
                   &clock->current_ds.offset_from_master,
                   &clock->current_ds.mean_path_delay);
      break;
    case P2P:
      ptp_sub_time(&clock->current_ds.offset_from_master,
                   &clock->current_ds.offset_from_master,
                   &clock->port_ds.peer_mean_path_delay);
      break;
    default:
      break;
  }

  if (clock->current_ds.offset_from_master.seconds != 0) {
    if (clock->port_ds.port_state == PTP_SLAVE)
      setFlag(clock->events, SYNCHRONIZATION_FAULT);
    DBGV("servo_update_offset: cannot filter seconds\n");
    return;
  }

  filter(&clock->current_ds.offset_from_master.nanoseconds, &clock->ofm_filt);

  if (abs(clock->current_ds.offset_from_master.nanoseconds) < PTPD_DEFAULT_CALIBRATED_OFFSET_NS) {
    if (clock->port_ds.port_state == PTP_UNCALIBRATED)
      setFlag(clock->events, MASTER_CLOCK_SELECTED);
  } else if (abs(clock->current_ds.offset_from_master.nanoseconds) > PTPD_DEFAULT_UNCALIBRATED_OFFSET_NS) {
    if (clock->port_ds.port_state == PTP_SLAVE)
      setFlag(clock->events, SYNCHRONIZATION_FAULT);
  }
}

void
servo_update_delay(ptp_clock_t* clock, const time_interval_t* delay_event_egress_timestamp,
                    const time_interval_t* recv_timestamp, const time_interval_t* correction_field)
{
  if (clock->ofm_filt.n == 0) {
    DBGV("servo_update_delay: Tms is not valid");
    return;
  }

  ptp_sub_time(&clock->time_sm, recv_timestamp, delay_event_egress_timestamp);
  ptp_sub_time(&clock->time_sm, &clock->time_sm, correction_field);
  ptp_time_add(&clock->current_ds.mean_path_delay, &clock->time_ms, &clock->time_sm);
  ptp_time_halve(&clock->current_ds.mean_path_delay);

  if (clock->current_ds.mean_path_delay.seconds == 0)
    filter(&clock->current_ds.mean_path_delay.nanoseconds, &clock->owd_filt);
}

void
servo_update_peer_delay(ptp_clock_t* clock, const time_interval_t* correction_field, bool is_two_step)
{
  DBGV("servo_update_peer_delay\n");

  if (is_two_step) {
    time_interval_t Tab, Tba;
    ptp_sub_time(&Tab, &clock->pdelay_t2, &clock->pdelay_t1);
    ptp_sub_time(&Tba, &clock->pdelay_t4, &clock->pdelay_t3);
    ptp_time_add(&clock->port_ds.peer_mean_path_delay, &Tab, &Tba);
  } else {
    ptp_sub_time(&clock->port_ds.peer_mean_path_delay, &clock->pdelay_t4, &clock->pdelay_t1);
  }

  ptp_sub_time(&clock->port_ds.peer_mean_path_delay, &clock->port_ds.peer_mean_path_delay, correction_field);
  ptp_time_halve(&clock->port_ds.peer_mean_path_delay);

  if (clock->port_ds.peer_mean_path_delay.seconds == 0)
    filter(&clock->port_ds.peer_mean_path_delay.nanoseconds, &clock->owd_filt);
}

void
servo_update_clock(ptp_clock_t* clock)
{
  int32_t adj;
  time_interval_t timeTmp;
  int32_t offsetNorm;

  DBGV("servo_update_clock\n");

  if (clock->current_ds.offset_from_master.seconds != 0 ||
      abs(clock->current_ds.offset_from_master.nanoseconds) > PTPD_MAX_ADJ_OFFSET_NS) {
    if (!clock->servo.no_adjust) {
      if (!clock->servo.no_reset_clock) {
        sys_get_clocktime(&timeTmp);
        ptp_sub_time(&timeTmp, &timeTmp, &clock->current_ds.offset_from_master);
        sys_set_clocktime(&timeTmp);
        servo_init_clock(clock);
      } else {
        adj = clock->current_ds.offset_from_master.nanoseconds > 0 ? ADJ_FREQ_MAX : -ADJ_FREQ_MAX;
        ptpd_adj_frequency(-adj);
      }
    }
  } else {
    offsetNorm = clock->current_ds.offset_from_master.nanoseconds;
    if (clock->port_ds.log_sync_interval > 0)
      offsetNorm >>= clock->port_ds.log_sync_interval;
    else if (clock->port_ds.log_sync_interval < 0)
      offsetNorm <<= -clock->port_ds.log_sync_interval;

    clock->observed_drift += offsetNorm / clock->servo.ai;
    clock->observed_drift = max(min(clock->observed_drift, ADJ_FREQ_MAX), -ADJ_FREQ_MAX);

    if (!clock->servo.no_adjust) {
      adj = offsetNorm / clock->servo.ap + clock->observed_drift;
      ptpd_adj_frequency(-adj);
    }

#if PTPD_DEFAULT_PARENTS_STATS == 1
    int a;
    int32_t scaledLogVariance;
    clock->parent_ds.parent_stats = TRUE;
    clock->parent_ds.observed_parent_clock_phase_change_rate = 1100 * clock->observed_drift;

    a = (clock->offset_history[1] - 2 * clock->offset_history[0] + clock->current_ds.offset_from_master.nanoseconds);
    clock->offset_history[1] = clock->offset_history[0];
    clock->offset_history[0] = clock->current_ds.offset_from_master.nanoseconds;

    scaledLogVariance = order(a * a) << 8;
    filter(&scaledLogVariance, &clock->slv_filt);
    clock->parent_ds.observed_parent_offset_scaled_log_variance = 17000 + scaledLogVariance;
#endif
  }

  switch (clock->port_ds.delay_mechanism) {
    case E2E:
      DBG("servo_update_clock: one-way delay averaged (E2E): %d sec %d nsec\n",
          clock->current_ds.mean_path_delay.seconds,
          clock->current_ds.mean_path_delay.nanoseconds);
      break;
    case P2P:
      DBG("servo_update_clock: one-way delay averaged (P2P): %d sec %d nsec\n",
          clock->port_ds.peer_mean_path_delay.seconds,
          clock->port_ds.peer_mean_path_delay.nanoseconds);
      break;
    default:
      DBG("servo_update_clock: one-way delay not computed\n");
  }

  DBG("servo_update_clock: offset from master: %d sec %d nsec\n",
      clock->current_ds.offset_from_master.seconds,
      clock->current_ds.offset_from_master.nanoseconds);
  DBG("servo_update_clock: observed drift: %d\n", clock->observed_drift);
}