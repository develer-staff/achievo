<?php

class scheduler_controller
{
  
  function &getInstance()
  {
    static $s_ins;
    if (is_null($s_ins))
    {
      $s_ins = new scheduler_controller();
    }
    return $s_ins;
  }

  /**
   * Constructor
   *
   */
  function controller()
  {
  }

  /**
    * handle HTTP requests
    *
    * @param $request array Array with the request vars
    */
  function handleRequest($request)
  {
    atkdebug("scheduler_controller::handleRequest");

    $model = &atkinstance("module.scheduler.scheduler_model");

    $view = &$this->getView($request["view"]);
    if($view!==false && is_object($view))
    {
      $ret = $view->render($request);
    }
    else 
    {
      $ret = atktext("view doesn't exist (".$request["view"]."view)");
    }
    return $ret;
  }
  
  function &getView($view)
  {
    $valid_views = array("day","week","month","birthday","pim");

    if(in_array($view,$valid_views))
    {
      atkdebug("scheduler_controller::getView -> View is $view");
      $view = &atkinstance("modules.scheduler.views.scheduler_".$view."view",$view);
      return $view;
    }
    atkdebug("scheduler_controller::getView -> $view is invalid");
    return false;
  }
  
  
  
}

?>