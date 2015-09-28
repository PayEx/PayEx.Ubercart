<?php
// $Id$

/**
 * @file
 * PayEx administration menu items.
 */

/**
 * Confirm PayEx Payment and update Ubercart order status
 * @param $order_id
 * @return void
 */
function uc_bankdebit_order_confirm($order_id) {
  // Check OrderRef
  if (empty($_GET['orderRef'])) {
    return;
  }

  // Get Px
  $px = new Px();
  $px->setEnvironment(
    variable_get('uc_bankdebit_account', ''),
    variable_get('uc_bankdebit_encryption_key', ''),
    variable_get('uc_bankdebit_testmode', 'yes') === 'yes'
  );

  // Call PxOrder.Complete
  $params = array(
    'accountNumber' => '',
    'orderRef' => $_GET['orderRef']
  );
  $result = $px->Complete($params);
  if ($result['errorCodeSimple'] !== 'OK') {
    $error_message = uc_bankdebit_extract_error_message($result);
    watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
    drupal_set_message($error_message, 'error');
    drupal_goto();
    return;
  }

  if (!isset($result['transactionNumber'])) {
    $result['transactionNumber'] = '';
  }

  // Check transaction is exists
  $transaction = uc_bankdebit_transaction_load($result['transactionNumber']);
  if ($transaction) {
    return;
  }

  // Save Transaction
  uc_bankdebit_transaction_add($order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

  // Load order
  $order = uc_order_load($order_id);

  /**
   * Transaction statuses:
   * 0=Sale, 1=Initialize, 2=Credit, 3=Authorize, 4=Cancel, 5=Failure, 6=Capture
   */
  switch ((int) $result['transactionStatus']) {
    case 0:
    case 6:
      $message = t('Transaction captured. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_payment_enter($order_id, $order->payment_method, $order->order_total, $order->uid, array('transaction_id' => $result['transactionNumber']), $message);
      uc_order_update_status($order_id, uc_order_state_default('completed'));

      $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
      break;
    case 1:
      $message = t('Transaction is pending. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');

      $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
      break;
    case 3:
      $message = t('Transaction authorized. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('payment_received'));

      $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
      drupal_goto('cart/checkout/complete');
      break;
    case 4:
      $message = t('Order was canceled');
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('canceled'));

      drupal_set_message($message, 'warning');
      drupal_goto('cart/checkout');
      break;
    case 5:
    default:
      // Cancel when Error
      $message = t('Transaction is failed.');
      if (!empty($result['transactionNumber'])) {
        $message = t('Transaction is failed. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
      }
      $message .= t('Details: @details', array('@details' => uc_bankdebit_extract_error_message($result)));

      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('canceled'));

      drupal_set_message($message, 'error');
      drupal_goto('cart/checkout');
      break;
  }
}

/**
 * Callback to cancel a payment
 * @param  $transaction_id
 * @return void
 */
function uc_bankdebit_cancel_payment($transaction_id) {
  $transaction = uc_bankdebit_transaction_load($transaction_id);
  if ($transaction) {
    // Load Order
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_bankdebit_account', ''),
      variable_get('uc_bankdebit_encryption_key', ''),
      variable_get('uc_bankdebit_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Cancel2
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction_id
    );
    $result = $px->Cancel2($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_bankdebit_extract_error_message($result);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_set_message($error_message, 'error');
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_bankdebit_transaction_add($transaction->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

    $message = t('Transaction canceled. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
    uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    uc_order_update_status($order->order_id, uc_order_state_default('canceled'));

    drupal_set_message($message);
  }

  // Trigger redirect destination.
  drupal_goto();
}

/**
 * Refund payments form
 * @param $form
 * @param $form_state
 * @param $order_id
 * @param $receipt_id
 * @return mixed
 */
function uc_bankdebit_refund_form($form, &$form_state, $order_id, $receipt_id) {
  $order = uc_order_load($order_id);
  if (!$order) {
    drupal_set_message(t('Order @order_id does not exist.', array('@order_id' => $order_id)));
    drupal_goto('admin/store/orders');
  }
  $payment = uc_payment_load($receipt_id);

  $form['amount'] = array(
    '#type' => 'textfield',
    '#title' => t('Amount'),
    '#description' => t('The amount to be refunded (must be a positive number)'),
    '#size' => 7,
    '#default_value' => (float) $payment->amount,
    '#required' => TRUE,
  );

  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Process Refund'),
  );

  $form['receipt_id'] = array(
    '#type' => 'hidden',
    '#value' => $receipt_id,
  );

  return $form;
}

/**
 * Refund payments form - validate
 * @param $form
 * @param $form_state
 * @return void
 */
function uc_bankdebit_refund_form_validate($form, &$form_state) {
  if (!is_numeric($form_state['values']['amount']) || $form_state['values']['amount'] < 0) {
    form_set_error('amount', t('You must enter a positive number for the amount.'));
  }
}

/**
 * Refund payments form - submit
 * @param $form
 * @param $form_state
 * @return void
 */
function uc_bankdebit_refund_form_submit($form, &$form_state) {
  $payment = uc_payment_load($form_state['values']['receipt_id']);
  $data = unserialize($payment->data);
  if ($data['transaction_id']) {
    $transaction = uc_bankdebit_transaction_load($data['transaction_id']);
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_bankdebit_account', ''),
      variable_get('uc_bankdebit_encryption_key', ''),
      variable_get('uc_bankdebit_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Credit5
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction->transaction_id,
      'amount' => round(100 * $form_state['values']['amount']),
      'orderId' => $order->order_id,
      'vatAmount' => 0,
      'additionalValues' => ''
    );
    $result = $px->Credit5($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_bankdebit_extract_error_message($result);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_set_message($error_message, 'error');
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_bankdebit_transaction_add($order->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

    $message = t('Payment refunded. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
    uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    uc_payment_enter($order->order_id, $order->payment_method, -1 * $form_state['values']['amount'], $order->uid, array('transaction_id' => $result['transactionNumber']), $message);

    drupal_set_message($message);
    $form_state['redirect'] = 'admin/store/orders/' . $order->order_id . '/payments/';
  }
}