(function($) {
    $.fn.funcPayExSSNResult = function(data) {
        if (data.status === 'error') {
            alert(data.message);
            return false;
        }

        $('input[name="panes[delivery][delivery_first_name]"]').val(data.first_name);
        $('input[name="panes[delivery][delivery_last_name]"]').val(data.last_name);
        $('input[name="panes[delivery][delivery_street1]"]').val(data.address_1);
        $('input[name="panes[delivery][delivery_street2]"]').val(data.address2);
        $('input[name="panes[delivery][delivery_city]"]').val(data.city);
        $('select[name="panes[delivery][delivery_country]"]').val(data.country_id).change();
        $('input[name="panes[delivery][delivery_postal_code]"]').val(data.postcode);

        $('input[name="panes[billing][billing_first_name]"]').val(data.first_name);
        $('input[name="panes[billing][billing_last_name]"]').val(data.last_name);
        $('input[name="panes[billing][billing_street1]"]').val(data.address_1);
        $('input[name="panes[billing][billing_street2]"]').val(data.address2);
        $('input[name="panes[billing][billing_city]"]').val(data.city);
        $('select[name="panes[billing][billing_country]"]').val(data.country_id).change();
        $('input[name="panes[billing][billing_postal_code]"]').val(data.postcode);
    };
})(jQuery);