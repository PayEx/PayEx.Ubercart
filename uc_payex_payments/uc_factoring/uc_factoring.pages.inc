<?php
// $Id$

/**
 * @file
 * PayEx administration menu items.
 */


/**
 * Callback to capture a payment
 * @param  $transaction_id
 * @return void
 */
function uc_factoring_capture_payment($transaction_id) {
  $transaction = uc_factoring_transaction_load($transaction_id);
  if ($transaction) {
    // Load Order
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_factoring_account', ''),
      variable_get('uc_factoring_encryption_key', ''),
      variable_get('uc_factoring_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Capture5
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction_id,
      'amount' => round(100 * $order->order_total),
      'orderId' => $order->order_id,
      'vatAmount' => 0,
      'additionalValues' => 'FINANCINGINVOICE_ORDERLINES=' . urlencode(uc_factoring_invoice_xml($order))
    );
    $result = $px->Capture5($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_factoring_extract_error_message($result);
      drupal_set_message($error_message);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_factoring_transaction_add($order->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

    $message = t('Transaction captured. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
    uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    uc_payment_enter($order->order_id, $order->payment_method, $order->order_total, $order->uid, array('transaction_id' => $result['transactionNumber']), $message);
    uc_order_update_status($order->order_id, uc_order_state_default('completed'));

    drupal_set_message($message);
  }

  // Trigger redirect destination.
  drupal_goto();
}

/**
 * Callback to cancel a payment
 * @param  $transaction_id
 * @return void
 */
function uc_factoring_cancel_payment($transaction_id) {
  $transaction = uc_factoring_transaction_load($transaction_id);
  if ($transaction) {
    // Load Order
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_factoring_account', ''),
      variable_get('uc_factoring_encryption_key', ''),
      variable_get('uc_factoring_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Cancel2
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction_id
    );
    $result = $px->Cancel2($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_factoring_extract_error_message($result);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_set_message($error_message, 'error');
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_factoring_transaction_add($transaction->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

    $message = t('Transaction canceled. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
    uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    uc_order_update_status($order->order_id, uc_order_state_default('canceled'));

    drupal_set_message($message);
  }

  // Trigger redirect destination.
  drupal_goto();
}

