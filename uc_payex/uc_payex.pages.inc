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
function uc_payex_order_confirm($order_id) {
  // Check OrderRef
  if (empty($_GET['orderRef'])) {
    return;
  }

  // Get Px
  $px = new Px();
  $px->setEnvironment(
    variable_get('uc_payex_account', ''),
    variable_get('uc_payex_encryption_key', ''),
    variable_get('uc_payex_testmode', 'yes') === 'yes'
  );

  // Call PxOrder.Complete
  $params = array(
    'accountNumber' => '',
    'orderRef' => $_GET['orderRef']
  );
  $result = $px->Complete($params);
  if ($result['errorCodeSimple'] !== 'OK') {
    $error_message = uc_payex_extract_error_message($result);
    watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
    drupal_set_message($error_message, 'error');
    drupal_goto();
    return;
  }

  if (!isset($result['transactionNumber'])) {
    $result['transactionNumber'] = '';
  }

  // Check transaction is exists
  $transaction = uc_payex_transaction_load($result['transactionNumber']);
  if ($transaction) {
    return;
  }

  // Save Transaction
  uc_payex_transaction_add($order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

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
      $message .= t('Details: @details', array('@details' => uc_payex_extract_error_message($result)));

      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('canceled'));

      drupal_set_message($message, 'error');
      drupal_goto('cart/checkout');
      break;
  }
}

/**
 * Process Transaction Callback from PayEx
 * @return void
 */
function uc_payex_tc() {
  @ob_clean();

  $_allowed_ips = array(
    '82.115.146.170', // Production
    '82.115.146.10' // Test
  );

  // Check is PayEx Request
  if (!in_array(ip_address(), $_allowed_ips)) {
    watchdog('PayEx TC', 'Access denied for this request. It\'s not PayEx Spider.', array(), WATCHDOG_WARNING);
    header(sprintf('%s %s %s', 'HTTP/1.1', '403', 'Access denied. Accept PayEx Transaction Callback only.'), TRUE, '403');
    header(sprintf('Status: %s %s', '403', 'Access denied. Accept PayEx Transaction Callback only.'), TRUE, '403');
    exit('Error: Access denied. Accept PayEx Transaction Callback only. ');
  }

  // Check Post Fields
  if (count($_POST) === 0 || empty($_POST['transactionNumber'])) {
    watchdog('PayEx TC', 'Empty request received.', array(), WATCHDOG_WARNING);
    header(sprintf('%s %s %s', 'HTTP/1.1', '500', 'FAILURE'), TRUE, '500');
    header(sprintf('Status: %s %s', '500', 'FAILURE'), TRUE, '500');
    exit('FAILURE');
  }

  // Get Px
  $px = new Px();
  $px->setEnvironment(
    variable_get('uc_payex_account', ''),
    variable_get('uc_payex_encryption_key', ''),
    variable_get('uc_payex_testmode', 'yes') === 'yes'
  );

  // Get Transaction Details
  $transactionId = $_POST['transactionNumber'];

  // Call PxOrder.GetTransactionDetails2
  $params = array(
    'accountNumber' => '',
    'transactionNumber' => $transactionId
  );
  $details = $px->GetTransactionDetails2($params);
  if ($details['code'] !== 'OK' || $details['description'] !== 'OK' || $details['errorCode'] !== 'OK') {
    watchdog('PayEx TC', 'Error:' . $details['errorCode'] . ' (' . $details['description'] . ')', array(), WATCHDOG_WARNING);
    exit('Error:' . $details['errorCode'] . ' (' . $details['description'] . ')');
  }

  $order_id = $details['orderId'];
  $transactionStatus = (int) $details['transactionStatus'];

  watchdog('PayEx TC', sprintf('Incoming transaction: %s, Transaction Status: %s, OrderId: %s ', $transactionId, $transactionStatus, $order_id), array(), WATCHDOG_NOTICE);

  // Load Order
  $order = uc_order_load($order_id);

  // Check orderID in Store
  if (!$order) {
    watchdog('PayEx TC', 'OrderID ' . $order_id . ' not found on store.', array(), WATCHDOG_WARNING);
    header(sprintf('%s %s %s', 'HTTP/1.1', '500', 'FAILURE'), TRUE, '500');
    header(sprintf('Status: %s %s', '500', 'FAILURE'), TRUE, '500');
    exit('FAILURE');
  }

  /* 0=Sale, 1=Initialize, 2=Credit, 3=Authorize, 4=Cancel, 5=Failure, 6=Capture */
  switch ($transactionStatus) {
    case 0;
    case 3:
      // Complete order
      $params = array(
        'accountNumber' => '',
        'orderRef' => $_POST['orderRef']
      );
      $result = $px->Complete($params);
      if ($result['errorCodeSimple'] !== 'OK') {
        watchdog('PayEx TC', 'Error:' . $details['errorCode'] . ' (' . $details['description'] . ')', array(), WATCHDOG_WARNING);
        exit('Error:' . $details['errorCode'] . ' (' . $details['description'] . ')');
      }

      // Save Transaction
      uc_payex_transaction_add($this->order->id, $result['transactionNumber'], $result['transactionStatus'], $result);

      switch ((int) $result['transactionStatus']) {
        case 0:
        case 6:
          $message = t('Transaction captured. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
          uc_order_comment_save($order_id, $order->uid, $message, 'admin');
          uc_payment_enter($order_id, $order->payment_method, $order->order_total, $order->uid, array('transaction_id' => $result['transactionNumber']), $message);
          uc_order_update_status($order_id, uc_order_state_default('completed'));
          break;
        case 1:
          $message = t('Transaction is pending. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
          uc_order_comment_save($order_id, $order->uid, $message, 'admin');
          break;
        case 3:
          $message = t('Transaction authorized. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
          uc_order_comment_save($order_id, $order->uid, $message, 'admin');
          uc_order_update_status($order_id, uc_order_state_default('payment_received'));
          break;
        case 4:
          $message = t('Order was canceled');
          uc_order_comment_save($order_id, $order->uid, $message, 'admin');
          uc_order_update_status($order_id, uc_order_state_default('canceled'));
          break;
        case 5:
        default:
          // Cancel when Errors
          $message = t('Transaction is failed.');
          if (!empty($result['transactionNumber'])) {
            $message = t('Transaction is failed. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
          }
          $message .= t('Details: @details', array('@details' => uc_payex_extract_error_message($result)));

          uc_order_comment_save($order_id, $order->uid, $message, 'admin');
          uc_order_update_status($order_id, uc_order_state_default('canceled'));
          break;
      }

      watchdog('PayEx TC', 'OrderId ' . $order_id . ' was completed with TransactionStatus ' . $result['transactionStatus'], array(), WATCHDOG_NOTICE);
      break;
    case 2:
      // Refund
      // Save Transaction
      uc_payex_transaction_add($order_id, $transactionId, $details['transactionStatus'], $details, isset($details['orderCreated']) ? strtotime($details['orderCreated']) : time());

      $credit_amount = (float) ($details['creditAmount'] / 100);
      $message = t('Payment refunded. Transaction Id: @transaction_id', array('@transaction_id' => $transactionId));
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_payment_enter($order_id, $order->payment_method, -1 * $credit_amount, $order->uid, array('transaction_id' => $transactionId), $message);
      watchdog('PayEx TC', 'OrderId ' . $order_id . ' refunded', array(), WATCHDOG_NOTICE);
      break;
    case 4;
      // Cancel
      // Save Transaction
      uc_payex_transaction_add($order_id, $transactionId, $details['transactionStatus'], $details, isset($details['orderCreated']) ? strtotime($details['orderCreated']) : time());

      $message = t('Order was canceled');
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('canceled'));

      watchdog('PayEx TC', 'OrderId ' . $order_id . ' canceled', array(), WATCHDOG_NOTICE);
      break;
    case 5:
      // Cancel when Errors
      // Save Transaction
      uc_payex_transaction_add($order_id, $transactionId, $details['transactionStatus'], $details, isset($details['orderCreated']) ? strtotime($details['orderCreated']) : time());

      $message = t('Transaction is failed. Transaction Id: @transaction_id', array('@transaction_id' => $transactionId));
      $message .= t('Details: @details', array('@details' => uc_payex_extract_error_message($details)));

      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_order_update_status($order_id, uc_order_state_default('canceled'));

      watchdog('PayEx TC', 'OrderId ' . $order_id . ' canceled', array(), WATCHDOG_NOTICE);
      break;
    case 6:
      // Set Order Status to captured
      // Save Transaction
      uc_payex_transaction_add($order_id, $transactionId, $details['transactionStatus'], $details, isset($details['orderCreated']) ? strtotime($details['orderCreated']) : time());

      $message = t('Transaction captured. Transaction Id: @transaction_id', array('@transaction_id' => $transactionId));
      uc_order_comment_save($order_id, $order->uid, $message, 'admin');
      uc_payment_enter($order_id, $order->payment_method, $order->order_total, $order->uid, array('transaction_id' => $transactionId), $message);
      uc_order_update_status($order_id, uc_order_state_default('completed'));

      watchdog('PayEx TC', 'OrderId ' . $order_id . ' captured', array(), WATCHDOG_NOTICE);
      break;
    default:
      watchdog('PayEx TC', 'Unknown Transaction Status', array(), WATCHDOG_WARNING);
      header(sprintf('%s %s %s', 'HTTP/1.1', '500', 'FAILURE'), TRUE, '500');
      header(sprintf('Status: %s %s', '500', 'FAILURE'), TRUE, '500');
      exit('FAILURE');
  }

  // Show "OK"
  header(sprintf('%s %s %s', 'HTTP/1.1', '200', 'OK'), TRUE, '200');
  header(sprintf('Status: %s %s', '200', 'OK'), TRUE, '200');
  exit('OK');
}

/**
 * Cancel Order
 * @return void
 */
function uc_payex_cancel() {
  if (isset($_SESSION['cart_order'])) {
    // Cancel order
    $order_id = (int) $_SESSION['cart_order'];
    uc_order_update_status($order_id, uc_order_state_default('canceled'));
    unset($_SESSION['cart_order']);
  }

  drupal_set_message(t('Order was canceled'));
  drupal_goto('cart');
}

/**
 * Callback to capture a payment
 * @param  $transaction_id
 * @return void
 */
function uc_payex_capture_payment($transaction_id) {
  $transaction = uc_payex_transaction_load($transaction_id);
  if ($transaction) {
    // Load Order
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_payex_account', ''),
      variable_get('uc_payex_encryption_key', ''),
      variable_get('uc_payex_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Capture5
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction_id,
      'amount' => round(100 * $order->order_total),
      'orderId' => $order->order_id,
      'vatAmount' => 0,
      'additionalValues' => ''
    );
    $result = $px->Capture5($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_payex_extract_error_message($result);
      drupal_set_message($error_message);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_payex_transaction_add($order->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

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
function uc_payex_cancel_payment($transaction_id) {
  $transaction = uc_payex_transaction_load($transaction_id);
  if ($transaction) {
    // Load Order
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_payex_account', ''),
      variable_get('uc_payex_encryption_key', ''),
      variable_get('uc_payex_testmode', 'yes') === 'yes'
    );

    // Call PxOrder.Cancel2
    $params = array(
      'accountNumber' => '',
      'transactionNumber' => $transaction_id
    );
    $result = $px->Cancel2($params);
    if ($result['code'] !== 'OK' || $result['description'] !== 'OK' || $result['errorCode'] !== 'OK') {
      $error_message = uc_payex_extract_error_message($result);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_set_message($error_message, 'error');
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_payex_transaction_add($transaction->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

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
function uc_payex_refund_form($form, &$form_state, $order_id, $receipt_id) {
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
function uc_payex_refund_form_validate($form, &$form_state) {
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
function uc_payex_refund_form_submit($form, &$form_state) {
  $payment = uc_payment_load($form_state['values']['receipt_id']);
  $data = unserialize($payment->data);
  if ($data['transaction_id']) {
    $transaction = uc_payex_transaction_load($data['transaction_id']);
    $order = uc_order_load($transaction->order_id);

    // Get Px
    $px = new Px();
    $px->setEnvironment(
      variable_get('uc_payex_account', ''),
      variable_get('uc_payex_encryption_key', ''),
      variable_get('uc_payex_testmode', 'yes') === 'yes'
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
      $error_message = uc_payex_extract_error_message($result);
      watchdog('PayEx error', $error_message, array(), WATCHDOG_WARNING);
      drupal_set_message($error_message, 'error');
      drupal_goto();
      return;
    }

    // Save Transaction
    uc_payex_transaction_add($order->order_id, $result['transactionNumber'], $result['transactionStatus'], $result);

    $message = t('Payment refunded. Transaction Id: @transaction_id', array('@transaction_id' => $result['transactionNumber']));
    uc_order_comment_save($order->order_id, $order->uid, $message, 'admin');
    uc_payment_enter($order->order_id, $order->payment_method, -1 * $form_state['values']['amount'], $order->uid, array('transaction_id' => $result['transactionNumber']), $message);

    drupal_set_message($message);
    $form_state['redirect'] = 'admin/store/orders/' . $order->order_id . '/payments/';
  }
}