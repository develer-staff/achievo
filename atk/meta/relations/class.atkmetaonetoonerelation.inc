<?php
/**
 * This file is part of the Achievo ATK distribution.
 * Detailed copyright and licensing information can be found
 * in the doc/COPYRIGHT and doc/LICENSE files which should be 
 * included in the distribution.
 *
 * @package atk
 * @subpackage meta.relations
 *
 * @copyright (c) 2005 Peter C. Verhage <peter@achievo.org>
 * @license http://www.achievo.org/atk/licensing ATK Open Source License
 *
 * @version $Revision: 1.7 $
 * $Id: class.atkmetaonetoonerelation.inc,v 1.7 2007/10/23 10:24:34 dennis Exp $
 */

/**
 * @internal Include the base class.
 */
userelation("atkonetoonerelation");
  
/**
 * Implementation of one-to-one relationships for meta nodes.
 *
 * @author Peter C. Verhage <peter@achievo.org>
 * @package atk
 * @subpackage meta.relations
 */        
class atkMetaOneToOneRelation extends atkOneToOneRelation
{
  var $m_accessor;
  var $m_options;
 
  /**
   * Default constructor.    
   */
  function atkMetaOneToOneRelation($name, $accessor, $template, $options, $flags=0)
  {
    $this->atkOneToOneRelation($name, $accessor, "?", $flags);
    $this->m_accessor = $accessor;
    $this->m_options = $options;
    
    $this->setDescriptorTemplate($template);
    if (isset($options["filter"])) 
      $this->setDestinationFilter($options["filter"]);   
  }
  
  /**
   * Create the instance of the destination. 
   *
   * If succesful, the instance is stored in the m_destInstance member variable.
   * 
   * @return boolean true if succesful, false if something went wrong.
   */
  function createDestination()
  {
    if ($this->m_destInstance != "") return TRUE;
    
    $policy = &$this->m_ownerInstance->getMetaPolicy();
    
    $this->m_destination = $policy->findDestination($this->m_accessor, false);
    atkdebug("atkMetaOneToOneRelation: {$this->m_accessor} destination {$this->m_destination}");        
    if ($this->m_destination == NULL) return FALSE;
    else if (!parent::createDestination()) return FALSE;
    
    $this->m_refKey = $policy->findDestinationAttribute($this);
    atkdebug("atkMetaOneToOneRelation: {$this->m_accessor} reference key {$this->m_refKey}");        
      
    return $this->m_refKey != NULL;
  }
}  
?>